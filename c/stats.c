typedef struct pe_run pe_run;
struct pe_run {
  unsigned ran1;
  double elapse1;
};

typedef struct pe_stat pe_stat;
struct pe_stat {
  int fsec, fsec15, fmin5;     /* first index of circular buffers */
  pe_run sec[15];
  pe_run sec15[20];
  pe_run min5[12];
};
