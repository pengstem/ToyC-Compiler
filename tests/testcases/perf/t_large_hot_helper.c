// A non-recursive helper deliberately just above the old loop-inline budget.
// The state stays bounded, so every arithmetic operation is defined in C.
int mix(int x) {
  x = (x * 3 + 7) % 10007;
  x = (x * 5 + 11) % 10007;
  x = (x * 7 + 13) % 10007;
  x = (x * 11 + 17) % 10007;
  x = (x * 13 + 19) % 10007;
  x = (x * 17 + 23) % 10007;
  x = (x * 19 + 29) % 10007;
  x = (x * 23 + 31) % 10007;
  x = (x * 29 + 37) % 10007;
  x = (x * 31 + 41) % 10007;
  x = (x * 37 + 43) % 10007;
  x = (x * 41 + 47) % 10007;
  x = (x * 43 + 53) % 10007;
  x = (x * 47 + 59) % 10007;
  x = (x * 53 + 61) % 10007;
  x = (x * 59 + 67) % 10007;
  x = (x * 61 + 71) % 10007;
  x = (x * 67 + 73) % 10007;
  x = (x * 71 + 79) % 10007;
  x = (x * 73 + 83) % 10007;
  x = (x * 79 + 89) % 10007;
  x = (x * 83 + 97) % 10007;
  x = (x * 89 + 101) % 10007;
  x = (x * 97 + 103) % 10007;
  return x;
}

int main() {
  int state = 1;
  int i = 0;
  while (i < 1000000) {
    state = mix(state);
    i = i + 1;
  }
  return state % 251;
}
