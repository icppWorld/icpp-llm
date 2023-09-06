// Inference endpoint generating a token string for a given prompt

#include "inference.h"

#include <string>
#include <unordered_map>
#include <variant>

#include "canister.h"
#include "http.h"
#include "initialize.h"
#include "run.h"
#include "upload.h"

#include "ic_api.h"

class Prompt {
public:
  std::string prompt{""};
  uint64_t steps{256};
  float temperature{1.0};
  float topp{0.9};
  uint64_t rng_seed{0};
};

void print_prompt(const Prompt &wire_prompt) {
  std::string msg = std::string(__func__) + "- model config:";
  msg += "\nwire_prompt.prompt       = " + wire_prompt.prompt;
  msg += "\nwire_prompt.steps        = " + std::to_string(wire_prompt.steps);
  msg +=
      "\nwire_prompt.temperature  = " + std::to_string(wire_prompt.temperature);
  msg += "\nwire_prompt.topp         = " + std::to_string(wire_prompt.topp);
  msg += "\nwire_prompt.rng_seed     = " + std::to_string(wire_prompt.rng_seed);
  IC_API::debug_print(msg);
}

// Replacement for run.c safe_printf
std::string safe_stringify(const char *piece) {
  std::string result;

  // piece might be a raw byte token, and we only want to convert printable chars or whitespace
  // because some of the other bytes can be various control codes, backspace, etc.
  if (piece == NULL) {
    return result;
  }

  if (piece[0] == '\0') {
    return result;
  }

  if (piece[1] == '\0') {
    unsigned char byte_val = piece[0];
    if (!(isprint(byte_val) || isspace(byte_val))) {
      return result; // bad byte, don't include it in the result
    }
  }

  result = piece; // Assign the valid string to the result
  return result;
}

// Copied from run.c and modified slightly
std::string generate(IC_API ic_api, Transformer *transformer,
                     Tokenizer *tokenizer, Sampler *sampler, std::string prompt,
                     int steps) {
  std::string output;

  // encode the (string) prompt into tokens sequence
  int num_prompt_tokens = 0;

  // +3 for '\0', ?BOS, ?EOS
  int *prompt_tokens = (int *)malloc((prompt.length() + 3) * sizeof(int));
  if (!prompt_tokens)
    IC_API::trap("Failed to allocate memory for prompt_tokens.");
  // We do not pass bos, but next, which is 1 after new_chat, else last token of previous call
  encode(tokenizer, prompt.c_str(), transformer->next, transformer->eos,
         prompt_tokens, &num_prompt_tokens);

  // icpp: make sure we do not re-add bos next time, unless reset by new_chat
  transformer->bos = 0;
  transformer->eos = 0;

  int token = transformer->next; // token that was predicted last, or BOS
  int pos = transformer->pos;    // position in the total sequence
  int prompt_pos = 0;            // position in the current prompt
  // if (num_prompt_tokens > 0) {
  //   token = prompt_tokens
  //       [prompt_pos]; // kick off with the first token in the prompt
  // }

  transformer->total_steps += num_prompt_tokens + steps;
  // override to ~max length
  if (transformer->total_steps > transformer->config.seq_len)
    transformer->total_steps = transformer->config.seq_len;

  // start the main loop
  long start =
      0;    // used to time our code, only initialized after first iteration
  int next; // will store the next token in the sequence
  // icpp: use -1, so the exact prompt will be returned when steps is 0
  //       this is critical when building the prompt in multiple calls
  while (pos < transformer->total_steps - 1) {

    // forward the transformer to get logits for the next token
    float *logits = forward(transformer, token, pos);

    // advance the state state machine
    if (prompt_pos < num_prompt_tokens - 1) {
      // if we are still processing the input prompt, force the next prompt token
      next = prompt_tokens[prompt_pos + 1];
      prompt_pos++;
    } else {
      // icpp: stop if we have exhausted the prompt_tokens, and caller did not ask for
      //       any additional steps to be generated
      if (steps == 0) {
        break;
      }
      // otherwise sample the next token from the logits
      next = sample(sampler, logits);
    }
    pos++;

    // data-dependent terminating condition: the BOS (=1) token delimits sequences
    if (next == 1) {
      break;
    }

    // print the token as string, decode it with the Tokenizer object
    char *piece = decode(tokenizer, token, next);
    // safe_printf(piece); // same as printf("%s", piece), but skips "unsafe" bytes
    output += safe_stringify(piece);

    // fflush(stdout);
    token = next;

    // safe state in the transformer, used in follow-up calls to the endpoint
    transformer->next = next;
    transformer->pos = pos;

    // init the timer here because the first iteration can be slower
    // if (start == 0) { start = time_in_ms(); }
  }
  // printf("\n");
  // output += "\n"; // icpp: don't do this... we're continuining in next call

  // report achieved tok/s (pos-1 because the timer starts after first iteration)
  // if (pos > 1) {
  //     long end = time_in_ms();
  //     fprintf(stderr, "achieved tok/s: %f\n", (pos-1) / (double)(end-start)*1000);
  // }

  free(prompt_tokens);

  return output;
}

// Based on a given prompt, llama2 will generate a token string
void inference() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  // IC_API::debug_print("ready_for_inference =" +
  //                     std::to_string(ready_for_inference));
  if (!ready_for_inference) {
    ic_api.to_wire(CandidTypeVariant{
        "err", CandidTypeText{
                   "The Llama2 canister is not (yet) ready for inference."}});
    return;
  }

  // Get the Prompt from the wire
  Prompt wire_prompt;
  CandidTypeRecord r_in;
  r_in.append("prompt", CandidTypeText{&wire_prompt.prompt});
  r_in.append("steps", CandidTypeNat64{&wire_prompt.steps});
  r_in.append("temperature", CandidTypeFloat32{&wire_prompt.temperature});
  r_in.append("topp", CandidTypeFloat32{&wire_prompt.topp});
  r_in.append("rng_seed", CandidTypeNat64{&wire_prompt.rng_seed});
  ic_api.from_wire(r_in);
  // print_prompt(wire_prompt);

  // parameter validation/overrides
  if (wire_prompt.rng_seed <= 0)
    wire_prompt.rng_seed = ic_api.time(); // time in ns
  if (wire_prompt.temperature < 0.0) wire_prompt.temperature = 0.0;
  if (wire_prompt.topp < 0.0 || 1.0 < wire_prompt.topp) wire_prompt.topp = 0.9;
  if (wire_prompt.steps < 0) wire_prompt.steps = 0;

  // icpp: if caller provides a prompt , set bos & eos
  // if (wire_prompt.prompt.size() > 0) {
  //   transformer.bos = 1;
  //   transformer.eos = 1;
  // }

  // icpp: We treat 'steps' as additional steps to generate
  //       Do this check inside generate method
  // if (wire_prompt.steps == 0 || wire_prompt.steps > transformer.config.seq_len)
  //   wire_prompt.steps = transformer.config.seq_len; // override to ~max length
  // IC_API::debug_print("--\nAfter parameter validation/overrides.");
  // print_prompt(wire_prompt);

  // build the Sampler
  Sampler sampler;
  build_sampler(&sampler, transformer.config.vocab_size,
                wire_prompt.temperature, wire_prompt.topp,
                wire_prompt.rng_seed);

  // run!
  std::string output;
  // if (mode == "generate") {
  output += generate(ic_api, &transformer, &tokenizer, &sampler,
                     wire_prompt.prompt, wire_prompt.steps);
  // } else if (mode =="chat") {
  // chat(&transformer, &tokenizer, &sampler, prompt, system_prompt, steps);
  // } else {
  //   IC_API::trap("unsupported mode: " + mode);
  // }

  // memory and file handles cleanup
  free_sampler(&sampler);

  IC_API::debug_print(output);

  // Return the generated response
  ic_api.to_wire(CandidTypeVariant{"ok", CandidTypeText{output}});
}
