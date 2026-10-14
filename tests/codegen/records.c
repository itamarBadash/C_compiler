int printf(const char *format, ...);
void *malloc(__SIZE_TYPE__ size);
void free(void *p);

struct point {
  int x, y;
};

struct shape {
  char tag;
  struct point corner[2];
  double area;
  const char *name;
};

union number {
  int i;
  float f;
  unsigned char bytes[4];
};

struct list {
  int value;
  struct list *next;
};

struct bag {
  int count;
  long items[];
};

static int sum_points(struct point *p, int n) {
  int total = 0;
  for (int i = 0; i < n; i++)
    total += p[i].x * 10 + p->y;
  return total;
}

static void move(struct point *p, int dx) {
  p->x += dx;
  (*p).y -= dx;
}

int main(void) {
  struct point a = {1, 2};
  struct point b = {.y = 7};
  struct point c;
  c = a;
  c.x += 5;
  printf("points %d %d %d %d %d %d\n", a.x, a.y, b.x, b.y, c.x, c.y);
  move(&c, 3);
  printf("moved %d %d\n", c.x, c.y);

  struct shape s = {'r', {{1, 2}, {3, 4}}, 2.5, "rect"};
  struct shape t = s;
  t.corner[1].y = 40;
  printf("shape %c %d %d %d %f %s\n", t.tag, t.corner[0].x, s.corner[1].y, t.corner[1].y, t.area,
         t.name);
  printf("sizes %d %d %d\n", (int)sizeof(struct point), (int)sizeof(struct shape),
         (int)sizeof(union number));

  struct point arr[3] = {{1, 1}, {2, 2}, [2].x = 9};
  printf("array %d %d\n", sum_points(arr, 3), arr[2].y);

  union number n;
  n.f = 1.0f;
  printf("union %x %d\n", n.i, n.bytes[3]);
  n.i = 0;
  n.bytes[0] = 7;
  printf("bytes %d\n", n.i);

  int flag = 1;
  struct point chosen = flag ? a : b;
  struct point other = (flag, b);
  printf("chosen %d %d\n", chosen.y, other.y);
  printf("member of rvalue %d\n", (flag ? a : b).x);

  struct list nodes[3];
  for (int i = 0; i < 3; i++) {
    nodes[i].value = i * i;
    nodes[i].next = i < 2 ? &nodes[i + 1] : 0;
  }
  int total = 0;
  for (struct list *p = nodes; p; p = p->next)
    total += p->value;
  printf("list %d\n", total);

  struct point *q = &(struct point){5, 6};
  q->x *= 2;
  printf("compound %d %d\n", q->x, q->y);
  int squares = 0;
  for (int i = 0; i < 3; i++) {
    int *row = (int[]){i, i * i, 0};
    squares += row[1];
    row[2] = 99;
  }
  printf("loop literal %d\n", squares);

  struct bag *bag = malloc(sizeof(struct bag) + 3 * sizeof(long));
  bag->count = 3;
  for (int i = 0; i < bag->count; i++)
    bag->items[i] = 100 + i;
  printf("flexible %ld %d\n", bag->items[2], (int)sizeof(struct bag));
  free(bag);

  struct point grid[2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
  struct point *g = &grid[1][0];
  printf("grid %d %d\n", g[1].x, (int)(&grid[1][1] - &grid[0][0]));
  volatile struct point vp = {3, 4};
  struct point copy = vp;
  printf("volatile %d\n", copy.y);
  return 0;
}
