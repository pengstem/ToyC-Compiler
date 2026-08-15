// `total` is not part of the branch key and has a non-additive recurrence.
// A repeated phase alone is therefore insufficient to accelerate this loop.
int main() {
  int phase = 0;
  int total = 3;
  int i = 0;
  while (i < 200) {
    if (phase == 0) phase = 1;
    else phase = 0;
    total = total * 3 + phase;
    i = i + 1;
  }
  return (total + phase) % 251;
}
