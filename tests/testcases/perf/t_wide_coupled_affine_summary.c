int main() {
    int i = 0;
    int s0 = 1;
    int s1 = 2;
    int s2 = 3;
    int s3 = 4;
    int s4 = 5;
    int s5 = 6;
    int s6 = 7;
    int s7 = 8;
    int s8 = 9;
    int s9 = 10;
    int s10 = 11;
    int s11 = 12;
    int s12 = 13;
    int s13 = 14;
    int s14 = 15;
    int s15 = 16;
    int s16 = 17;
    int s17 = 18;
    int s18 = 19;
    int s19 = 20;
    int s20 = 21;
    int s21 = 22;
    int s22 = 23;
    int s23 = 24;
    int s24 = 25;
    while (i < 10000000) {
        s0 = s1;
        s1 = s2;
        s2 = s3;
        s3 = s4;
        s4 = s5;
        s5 = s6;
        s6 = s7;
        s7 = s8;
        s8 = s9;
        s9 = s10;
        s10 = s11;
        s11 = s12;
        s12 = s13;
        s13 = s14;
        s14 = s15;
        s15 = s16;
        s16 = s17;
        s17 = s18;
        s18 = s19;
        s19 = s20;
        s20 = s21;
        s21 = s22;
        s22 = s23;
        s23 = s24;
        s24 = s0;
        i = i + 1;
    }
    return (s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8 + s9 + s10 + s11 +
            s12 + s13 + s14 + s15 + s16 + s17 + s18 + s19 + s20 + s21 + s22 +
            s23 + s24 + i) % 251;
}
