"""Uploads stories15M.bin & tokenizer.bin to the canister.

Run with:

    python -m scripts.upload
"""

# pylint: disable=invalid-name, too-few-public-methods, no-member, too-many-statements

import sys
from pathlib import Path
from typing import Generator
from .ic_py_canister import get_canister
from .parse_args import parse_args

ROOT_PATH = Path(__file__).parent.parent

#  0 - none
#  1 - minimal
#  2 - a lot
DEBUG_VERBOSE = 1


# ------------------------------------------------------------------------------
def read_file_bytes(file_path: Path) -> bytes:
    """Returns the stories15M.bin file as a bytes array"""
    file_bytes = b""
    try:
        with open(file_path, "rb") as file:
            file_bytes = file.read()

    except FileNotFoundError:
        print(f"Unable to open the file {file_path}!")

    return file_bytes


def generate_chunks(data: bytes, chunk_size: int) -> Generator[bytes, None, None]:
    """Generator function to iterate over chunks"""
    for i in range(0, len(data), chunk_size):
        yield data[i : i + chunk_size]


def main() -> int:
    """Uploads the model."""

    args = parse_args()

    network = args.network
    canister_name = args.canister
    candid_path = ROOT_PATH / args.candid
    model_path = ROOT_PATH / args.model
    tokenizer_path = ROOT_PATH / args.tokenizer
    chunk_size_mb = args.chunksize
    temperature = args.temperature
    topp = args.topp
    steps = args.steps
    rng_seed = args.rng_seed

    dfx_json_path = ROOT_PATH / "dfx.json"

    print(
        f"Summary of upload task:"
        f"\n - network        = {network}"
        f"\n - canister       = {canister_name}"
        f"\n - dfx_json_path  = {dfx_json_path}"
        f"\n - candid_path    = {candid_path}"
        f"\n - model_path     = {model_path}"
        f"\n - tokenizer_path = {tokenizer_path}"
        f"\n - temperature    = {temperature}"
        f"\n - topp           = {topp}"
        f"\n - steps          = {steps}"
        f"\n - rng_seed       = {rng_seed}"
    )

    # ---------------------------------------------------------------------------
    # get ic-py based Canister instance
    canister_llama2 = get_canister(canister_name, candid_path, network)

    # check health (liveness)
    print("--\nChecking liveness of canister (did we deploy it!)")
    response = canister_llama2.health()
    if response == [True]:
        print("Ok!")
    else:
        print("Not OK, response is:")
        print(response)

    # ---------------------------------------------------------------------------
    # THE MODEL FILE

    # Read the model from disk
    print(f"--\nReading the model file into a bytes object: {model_path}")
    model_bytes = read_file_bytes(model_path)

    # Reset the model
    print("--\nResetting the model in canister")
    response = canister_llama2.reset_model()  # pylint: disable=no-member
    if "ok" in response[0].keys():
        if DEBUG_VERBOSE >= 2:
            print("OK!")
    else:
        print("Something went wrong:")
        print(response)
        sys.exit()

    # Upload model_bytes to the canister
    print(f"--\nUploading the model bytes, in {chunk_size_mb}Mb chunks")

    # converting MB to bytes
    chunk_size = int(chunk_size_mb * 1024 * 1024)

    # Iterate over all chunks
    count_bytes = 0
    for i, chunk in enumerate(generate_chunks(model_bytes, chunk_size)):
        count_bytes += len(chunk)
        if DEBUG_VERBOSE == 0:
            pass
        elif DEBUG_VERBOSE == 1:
            print(
                f"chunk size = {len(chunk)} bytes "
                f"({count_bytes / len(model_bytes) * 100:.1f}%)"
            )
        else:
            print("+++++++++++++++++++++++++++++++++++++++++++++++++++++")
            print(f"Sending candid for {len(chunk)} bytes :")
            print(f"- i         = {i}")
            print(f"- progress  = {count_bytes / len(model_bytes) * 100:.1f}% ")
            print(f"- chunk[0]  = {chunk[0]}")
            print(f"- chunk[-1] = {chunk[-1]}")

        response = canister_llama2.upload_model_bytes_chunk(
            chunk
        )  # pylint: disable=no-member
        if "ok" in response[0].keys():
            if DEBUG_VERBOSE >= 2:
                print("OK!")
        else:
            print("Something went wrong:")
            print(response)
            sys.exit()

    # ---------------------------------------------------------------------------
    # THE TOKENIZER FILE

    # Read the tokenizer from disk
    print(f"--\nReading the tokenizer file into a bytes object: {tokenizer_path}")
    tokenizer_bytes = read_file_bytes(tokenizer_path)

    # Reset the tokenizer
    print("--\nResetting the tokenizer in canister")
    response = canister_llama2.reset_tokenizer()  # pylint: disable=no-member
    if "ok" in response[0].keys():
        if DEBUG_VERBOSE >= 2:
            print("OK!")
    else:
        print("Something went wrong:")
        print(response)
        sys.exit()

    # Upload tokenizer_bytes to the canister
    print("--\nUploading the tokenizer bytes")

    # converting MB to bytes
    chunk_size = int(chunk_size_mb * 1024 * 1024)

    # Iterate over all chunks
    count_bytes = 0
    for i, chunk in enumerate(generate_chunks(tokenizer_bytes, chunk_size)):
        count_bytes += len(chunk)
        if DEBUG_VERBOSE == 0:
            pass
        elif DEBUG_VERBOSE == 1:
            print(
                f"chunk size = {len(chunk)} bytes "
                f"({count_bytes / len(tokenizer_bytes) * 100:.1f}%)"
            )
        else:
            print("+++++++++++++++++++++++++++++++++++++++++++++++++++++")
            print(f"Sending candid for {len(chunk)} bytes :")
            print(f"- i         = {i}")
            print(f"- progress  = {count_bytes / len(tokenizer_bytes) * 100:.1f} % ")
            print(f"- chunk[0]  = {chunk[0]}")
            print(f"- chunk[-1] = {chunk[-1]}")

        response = canister_llama2.upload_tokenizer_bytes_chunk(
            chunk
        )  # pylint: disable=no-member
        if "ok" in response[0].keys():
            print("OK!")
        else:
            print("Something went wrong:")
            print(response)
            sys.exit()

    # ---------------------------------------------------------------------------
    # Initialize the canister
    print("--\nInitializing the canister, getting it ready for inference.")
    response = canister_llama2.initialize()
    if "ok" in response[0].keys():
        if DEBUG_VERBOSE >= 2:
            print("OK!")
    else:
        print("Something went wrong:")
        print(response)
        sys.exit()

    # ---------------------------------------------------------------------------
    # check readiness for inference
    print("--\nChecking if the canister is ready for inference.")
    response = canister_llama2.ready()
    if response[0] is True:
        if DEBUG_VERBOSE >= 2:
            print("OK!")
    else:
        print("Something went wrong:")
        print(response)
        sys.exit()
    # ---------------------------------------------------------------------------
    print("--\nCongratulations, your llama2 canister is ready for inference!")
    try:
        print("💯 🎉 🏁")
    except UnicodeEncodeError:
        print(" ")

    # ---------------------------------------------------------------------------
    return 0


if __name__ == "__main__":
    sys.exit(main())
