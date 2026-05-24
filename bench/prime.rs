/* prime.rs — Count primes up to N (Rust reference implementation for the benchmark)
   Compile: rustc -C opt-level=3 -o prime_rs prime.rs
   Usage:   ./prime_rs --n 500000
*/

fn is_prime(n: u32) -> bool {
    if n < 2 { return false; }
    if n == 2 { return true; }
    if n % 2 == 0 { return false; }
    let mut i = 3u32;
    while i * i <= n {
        if n % i == 0 { return false; }
        i += 2;
    }
    true
}

fn count_primes(limit: u32) -> u32 {
    (2..=limit).filter(|&n| is_prime(n)).count() as u32
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut n: u32 = 500000;
    let mut i = 1;
    while i + 1 < args.len() {
        if args[i] == "--n" {
            n = args[i + 1].parse().unwrap_or(500000);
        }
        i += 1;
    }
    println!("Primes up to {}: {}", n, count_primes(n));
}
