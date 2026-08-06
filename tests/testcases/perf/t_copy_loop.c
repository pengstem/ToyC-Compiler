int main() {
  int seed = 5;
  int first = seed;
  int second = first;
  int third = second;
  int value = third;
  int sum = 0;
  int i = 0;
  while (i < 1000) {
    int a = i;
    int b = a;
    int c = b;
    sum = sum + c;
    i = i + 1;
  }
  return (sum + value) % 251;
}
