int advance_twice(int value) {
  value = value + 3;
  int result = value + 3;
  return result;
}

int main() {
  return advance_twice(1);
}
