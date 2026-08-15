int affine_chain() {
  int total = 0;
  int i = 0;
  while (i < 20000) {
    int left = total + i * 11;
    total = left + i * 17;
    i = i + 1;
  }
  return total;
}

int polynomial_locals() {
  int total = 0;
  int i = 0;
  while (i < 6000) {
    int a = i * 3 + 17;
    int shifted = i + 5;
    int square = shifted * shifted;
    total = total + a + square;
    i = i + 1;
  }
  return total;
}

int constant_branch_dead_code() {
  int live = 1;
  int i = 0;
  while (i < 5000) {
    int dead = (i * i + 100) % 997;
    if (0) live = live + dead;
    if (1) live = live + i;
    i = i + 1;
  }
  return live;
}

int main() {
  return (affine_chain() + polynomial_locals() + constant_branch_dead_code()) % 251;
}
