[![icpp-llm](https://github.com/icppWorld/icpp-llm/actions/workflows/cicd.yml/badge.svg)](https://github.com/icppWorld/icpp-llm/actions/workflows/cicd.yml)

# LLMs for the Internet Computer

Video: [How to run llama2.c on the Internet Computer](https://www.loom.com/share/a065b678df63462fb2f637d1b550b5d2?sid=1aeee693-25c0-4d1f-be0c-8231b53eb742)

# Try it

`icpp_llama2` with the stories15M.bin is running on-chain in canister `4c4bn-daaaa-aaaag-abvcq-cai`. 

You can call it's inference endpoint with:

```bash
dfx canister call --network ic 4c4bn-daaaa-aaaag-abvcq-cai inference '(record {prompt = "" : text; steps = 20 : nat64; temperature
 = 0.8 : float32; topp = 1.0 : float32;})'
(
  variant {
    ok = "Once upon a time, there was a little boat named Bob. Bob loved to float on the water"
  },
)
```


## The LLMs included in this repo

| LLM folder        | reference                                                      |
| ------------- | ------------------------------------------------------------ |
| [icpp_llama2](https://github.com/icppWorld/icpp-llm/tree/main/icpp_llama2)   | [karpathy/llama2.c](https://github.com/karpathy/llama2.c) |


## Instructions

See the README in each LLM folder