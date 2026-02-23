fn product<I>(iter: I) -> i32
where
    I: Iterator<Item = i32>,
{
    iter.product()
}

fn main() {
    for i in 1..=12 {
        print!("{}", product(1..=i));
    }
}