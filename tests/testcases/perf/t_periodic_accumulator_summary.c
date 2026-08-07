int bucket(int value) {
    int residue = value % 100;
    return (residue * residue + residue + 3) % 17;
}

int periodic_sum(int seed) {
    int index = 3;
    int sum = seed;
    int difference = seed + 19;
    while (index < 100000003) {
        int residue = index % 20;
        sum = sum + bucket(index);
        difference = difference - (residue * 3 + 5) % 23;
        index = index + 1;
    }
    return sum + difference + index;
}

int main() {
    return periodic_sum(11) % 251;
}
