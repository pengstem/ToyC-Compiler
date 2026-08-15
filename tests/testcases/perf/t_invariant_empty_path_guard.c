// The outer equality is invariant in the inner loop. When it is false, the
// short-circuit path only advances j, so the whole empty inner loop can be skipped.
int connected(int i, int j) {
  return (i + j) % 3 != 0;
}

int main() {
  int found1 = 0;
  int found2 = 0;
  int i = 0;
  while (i < 1000) {
    int j = 0;
    while (j < 1000) {
      if (i == 0 && connected(i, j)) {
        if (j == 1) found1 = 1;
        if (j == 2) found2 = 1;
      }
      j = j + 1;
    }
    i = i + 1;
  }
  return found1 + found2;
}
