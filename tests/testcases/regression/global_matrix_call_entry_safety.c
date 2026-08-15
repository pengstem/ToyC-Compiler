int global_a = 1;
int global_b = 2;
int global_c = 3;

void reset_globals(int depth) {
  if (depth > 0) {
    reset_globals(depth - 1);
  } else {
    global_a = 4;
    global_b = 5;
    global_c = 6;
  }
}

int main() {
  reset_globals(2);
  int i = 0;
  while (i < 1000) {
    int next_a = (global_a * 7 + global_b * 11 + global_c * 13) % 10007;
    int next_b = (global_a * 17 + global_b * 19 + global_c * 23) % 10007;
    int next_c = (global_a * 29 + global_b * 31 + global_c * 37) % 10007;
    global_a = next_a;
    global_b = next_b;
    global_c = next_c;
    i = i + 1;
  }
  return (global_a + global_b + global_c + i) % 251;
}
