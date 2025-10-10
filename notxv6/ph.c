#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define NBUCKET 5
#define NKEYS 100000

struct entry
{
  int key;
  int value;
  struct entry *next;
};
// struct entry *table[NBUCKET];
struct hash_table
{
  pthread_mutex_t locks[NBUCKET];
  struct entry *table[NBUCKET];
};
struct hash_table ht;

int keys[NKEYS];
int nthread = 1;

double
now()
{
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void
insert(int key, int value, struct entry **p, struct entry *n)
{
  struct entry *e = malloc(sizeof(struct entry));
  e->key = key;
  e->value = value;
  e->next = n;
  *p = e; // 地址更新
}

static void put(int key, int value)
{
  int i = key % NBUCKET;

  pthread_mutex_lock(&ht.locks[i]);
  // is the key already present?
  struct entry *e = 0;
  for (e = ht.table[i]; e != 0; e = e->next)
  {
    if (e->key == key)
      break;
  }
  if (e)
  {
    // update the existing key.
    e->value = value;
  }
  else
  {
    // the new is new.
    insert(key, value, &ht.table[i], ht.table[i]);
  }

  pthread_mutex_unlock(&ht.locks[i]);
}

static struct entry *
get(int key)
{
  int i = key % NBUCKET;
  pthread_mutex_lock(&ht.locks[i]);
  struct entry *e = 0;
  for (e = ht.table[i]; e != 0; e = e->next)
  {
    if (e->key == key)
      break;
  }
  pthread_mutex_unlock(&ht.locks[i]);
  return e;
}

static void *
put_thread(void *xa)
{
  int n = (int)(long)xa; // thread number
  int b = NKEYS / nthread;

  for (int i = 0; i < b; i++)
  {
    put(keys[b * n + i], n);
  }

  return NULL;
}

static void *
get_thread(void *xa)
{
  int n = (int)(long)xa; // thread number
  int missing = 0;

  for (int i = 0; i < NKEYS; i++)
  {
    struct entry *e = get(keys[i]);
    if (e == 0)
      missing++;
  }
  printf("%d: %d keys missing\n", n, missing);
  return NULL;
}

int main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  double t1, t0;

  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]); // 开启的用户级线程数量
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  // 循环初始化每个桶的锁
  for (int i = 0; i < NBUCKET; i++)
  {
    pthread_mutex_init(&ht.locks[i], NULL);
  }

  assert(NKEYS % nthread == 0);
  for (int i = 0; i < NKEYS; i++)
  {
    keys[i] = random();
  }

  //
  // first the puts
  //
  t0 = now();
  for (int i = 0; i < nthread; i++)
  {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *)(long)i) == 0);
  }
  for (int i = 0; i < nthread; i++)
  {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         NKEYS, t1 - t0, NKEYS / (t1 - t0));

  //
  // now the gets
  //
  t0 = now();
  for (int i = 0; i < nthread; i++)
  {
    assert(pthread_create(&tha[i], NULL, get_thread, (void *)(long)i) == 0);
  }
  for (int i = 0; i < nthread; i++)
  {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d gets, %.3f seconds, %.0f gets/second\n",
         NKEYS * nthread, t1 - t0, (NKEYS * nthread) / (t1 - t0));
}
