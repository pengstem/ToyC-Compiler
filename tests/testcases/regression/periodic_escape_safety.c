int main() {
    int index = 0;
    int residue = 0;
    int sum = 0;
    while (index < 10000) {
        residue = index % 17;
        sum = sum + residue;
        index = index + 1;
    }
    return (sum + residue) % 251;
}
