int main() {
    int index = 0;
    int result = 0;
    int witness = 7;
    while (index < 100000) {
        int residue = (index + 91) % 127;
        result = ((result + residue * residue) % 1000003 - index % 1000003) % 1000003;
        witness = witness + result;
        index = index + 1;
    }
    return result + witness;
}
