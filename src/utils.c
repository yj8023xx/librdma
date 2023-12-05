#include "utils.h"

//===========================================================================
//=  Function to generate uniformly distributed random variables            =
//=    - Input:  Min and max values                                         =
//=    - Output: Returns with uniformly distributed random variable         =
//===========================================================================
int uniform(int min, int max) {
  int z;           // Uniform random integer number
  int unif_value;  // Computed uniform value to be returned

  // Pull a uniform random integer
  z = rand();

  // Compute uniform discrete random variable using inversion method
  unif_value = z % (max - min + 1) + min;

  return (unif_value);
}

//===========================================================================
//=  Function to generate Zipf (power law) distributed random variables     =
//=    - Input: alpha and N                                                 =
//=    - Output: Returns with Zipf distributed random variable              =
//===========================================================================
int zipf(double alpha, int n) {
  static int first = true;  // Static first time flag
  static double c = 0;      // Normalization constant
  double z;                 // Uniform random number (0 < z < 1)
  double sum_prob;          // Sum of probabilities
  double zipf_value;        // Computed exponential value to be returned
  int i;                    // Loop counter

  // Compute normalization constant on first call only
  if (first == true) {
    for (i = 1; i <= n; i++) c = c + (1.0 / pow((double)i, alpha));
    c = 1.0 / c;
    first = false;
  }

  // Pull a uniform random number (0 < z < 1)
  do {
    z = (double)rand() /
        RAND_MAX;  // generate a random number in the range of [0,1]
  } while ((z == 0) || (z == 1));

  // Map z to the value
  sum_prob = 0;
  for (i = 1; i <= n; i++) {
    sum_prob = sum_prob + c / pow((double)i, alpha);
    if (sum_prob >= z) {
      zipf_value = i;
      break;
    }
  }

  // Assert that zipf_value is between 1 and N
  // assert((zipf_value >= 1) && (zipf_value <= n));

  return (zipf_value);
}

// 64位MurmurHash算法
uint64_t murmurhash64(const void *key, int len, uint64_t seed) {
  const uint64_t m = 0xc6a4a7935bd1e995ULL;
  const int r = 47;

  uint64_t h = seed ^ (len * m);

  const uint64_t *data = (const uint64_t *)key;
  const uint64_t *end = data + (len / 8);

  while (data != end) {
    uint64_t k = *data++;

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }

  const unsigned char *data2 = (const unsigned char *)data;

  switch (len & 7) {
    case 7:
      h ^= (uint64_t)(data2[6]) << 48;
    case 6:
      h ^= (uint64_t)(data2[5]) << 40;
    case 5:
      h ^= (uint64_t)(data2[4]) << 32;
    case 4:
      h ^= (uint64_t)(data2[3]) << 24;
    case 3:
      h ^= (uint64_t)(data2[2]) << 16;
    case 2:
      h ^= (uint64_t)(data2[1]) << 8;
    case 1:
      h ^= (uint64_t)(data2[0]);
      h *= m;
  };

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}