struct pair {
  int tag;
  union {
    long long whole;
    struct {
      unsigned low;
      int high;
    };
  };
};
