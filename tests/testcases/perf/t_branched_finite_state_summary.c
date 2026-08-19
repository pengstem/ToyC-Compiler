int node = 0;
int distance = 0;

int main() {
  int depth = 0;
  while (depth < 30000) {
    int next = 0;
    if (node == 0) next = 1;
    else if (node == 1) next = 2;
    else if (node == 2) {
      if (distance % 2 == 0) next = 3;
      else next = 4;
    } else if (node == 3) next = 1;
    else next = 0;
    node = next;
    distance = distance + node + 1;
    depth = depth + 1;
  }
  return (node + distance) % 251;
}
