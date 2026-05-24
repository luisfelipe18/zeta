"""prime.py — Count primes up to N (Python reference implementation for the benchmark)
Usage: python3 prime.py --n 500000
"""
import sys


def is_prime(n: int) -> bool:
    if n < 2:
        return False
    if n == 2:
        return True
    if n % 2 == 0:
        return False
    i = 3
    while i * i <= n:
        if n % i == 0:
            return False
        i += 2
    return True


def count_primes(limit: int) -> int:
    return sum(1 for num in range(2, limit + 1) if is_prime(num))


def main() -> None:
    n = 500000
    args = sys.argv[1:]
    for i, arg in enumerate(args):
        if arg == "--n" and i + 1 < len(args):
            n = int(args[i + 1])

    result = count_primes(n)
    print(f"Primes up to {n}: {result}")


if __name__ == "__main__":
    main()
