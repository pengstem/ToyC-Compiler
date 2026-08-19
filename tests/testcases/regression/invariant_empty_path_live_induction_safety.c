// Even when the guarded loop body is empty, skipping the loop is unsafe if its
// final induction value is observed after the exit.
int main() {
  int outer = 1;
  int touched = 0;
  int i = 0;
  while (i < 100) {
    if (outer == 0 && i == 1) touched = 1;
    i = i + 1;
  }
  return i + touched;
}
