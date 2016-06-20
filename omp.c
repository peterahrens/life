#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <immintrin.h>
#include <omp.h>

#ifndef MAX
#define MAX(A,B) ((A)>(B)?(A):(B))
#endif

#ifndef MIN
#define MIN(A,B) ((A)<(B)?(A):(B))
#endif

#define             WORD (256/8)
#define        OUT_GHOST WORD * 1
#define         IN_GHOST (OUT_GHOST + 1)
#define       X_IN_GHOST ((OUT_GHOST/WORD + 1) * WORD) //should be multiple of word size
#define       Y_IN_GHOST IN_GHOST
#define X_IN_GHOST_WORDS (X_IN_GHOST/WORD)

unsigned *life (const unsigned height,
                const unsigned width,
                const unsigned * const initial,
                const unsigned iters,
                const unsigned display) {
  const unsigned padded_height = height + 2 * Y_IN_GHOST;
  const unsigned padded_width = width + 2 * X_IN_GHOST;
  const unsigned width_words = width/WORD;
  const unsigned padded_width_words = padded_width/WORD;
  uint8_t *universe = (uint8_t*)malloc(padded_height * padded_width);
  uint8_t *new = (uint8_t*)malloc(padded_height * padded_width);
  unsigned *out = (unsigned*)malloc(sizeof(unsigned) * height * width);

  omp_set_num_threads(omp_get_num_procs());
  #pragma omp parallel
  {
    uint8_t *my_universe = universe;
    uint8_t *my_new = new;
    //pack into padded working array
    /*
    unsigned num_threads = omp_get_num_threads();
    unsigned thread_num = omp_get_thread_num();
    unsigned start = padded_height/num_threads * thread_num;
    unsigned end = padded_height/num_threads * (thread_num + 1);
    */
    #pragma omp for
    for (unsigned y = Y_IN_GHOST; y < height + Y_IN_GHOST; y++) {
      for (unsigned x = X_IN_GHOST; x < width + X_IN_GHOST; x++) {
        my_universe[(y * padded_width) + x] = initial[(y - Y_IN_GHOST) * width + x - X_IN_GHOST];
      }
    }
    for (unsigned i = 0; i < iters; i++) {
      //copy the ghost cells once every IN_GHOST iterations
      if (i % IN_GHOST == 0){
        __m256i *my_universe_words = (__m256i*)my_universe;
        #pragma omp for
        for (unsigned y = 0; y < padded_height; y++) {
          if (y < Y_IN_GHOST) {
            for (unsigned x = 0; x < X_IN_GHOST_WORDS; x++) {
              _mm256_store_si256(my_universe_words + y * padded_width_words + x,
                _mm256_load_si256(my_universe_words + (y + height) * padded_width_words + x + width_words));
            }
            for (unsigned x = X_IN_GHOST_WORDS; x < width_words + X_IN_GHOST_WORDS; x++) {
              _mm256_store_si256(my_universe_words + y * padded_width_words + x,
                _mm256_load_si256(my_universe_words + (y + height) * padded_width_words + x));
            }
            for (unsigned x = width_words + X_IN_GHOST_WORDS ; x < padded_width_words; x++) {
              _mm256_store_si256(my_universe_words + y * padded_width_words + x,
                _mm256_load_si256(my_universe_words + (y + height) * padded_width_words + x - width_words));
            }
          } else if (y < height + Y_IN_GHOST) {
            for (unsigned x = 0; x < X_IN_GHOST_WORDS; x++) {
              _mm256_store_si256(my_universe_words + y * padded_width_words + x,
                _mm256_load_si256(my_universe_words + y * padded_width_words + x + width_words));
            }
            for (unsigned x = width_words + X_IN_GHOST_WORDS ; x < padded_width_words; x++) {
              _mm256_store_si256(my_universe_words + y * padded_width_words + x,
                _mm256_load_si256(my_universe_words + y * padded_width_words + x - width_words));
            }
          } else {
            for (unsigned x = 0; x < X_IN_GHOST_WORDS; x++) {
              _mm256_store_si256(my_universe_words + y * padded_width_words + x,
                _mm256_load_si256(my_universe_words + (y - height) * padded_width_words + x + width_words));
            }
            for (unsigned x = X_IN_GHOST_WORDS; x < width_words + X_IN_GHOST_WORDS; x++) {
              _mm256_store_si256(my_universe_words + y * padded_width_words + x,
                _mm256_load_si256(my_universe_words + (y - height) * padded_width_words + x));
            }
            for (unsigned x = width_words + X_IN_GHOST_WORDS ; x < padded_width_words; x++) {
              _mm256_store_si256(my_universe_words + y * padded_width_words + x,
                _mm256_load_si256(my_universe_words + (y - height) * padded_width_words + x - width_words));
            }
          }
        }
      }
      //evolve
      const __m256i ones = _mm256_set_epi8(1, 1, 1, 1, 1, 1, 1, 1,
                                     1, 1, 1, 1, 1, 1, 1, 1,
                                     1, 1, 1, 1, 1, 1, 1, 1,
                                     1, 1, 1, 1, 1, 1, 1, 1);
      const __m256i twos = _mm256_slli_epi32(ones, 1);
      const __m256i threes = _mm256_or_si256(ones, twos);
      #pragma omp for
      for (unsigned y = Y_IN_GHOST - OUT_GHOST; y < height + Y_IN_GHOST + OUT_GHOST; y++) {
        for (unsigned x = (X_IN_GHOST - OUT_GHOST); x + WORD <= width + X_IN_GHOST + OUT_GHOST; x += WORD) {
          __m256i n;
          __m256i alive;
          uint8_t *u = my_universe + (y - 1) * padded_width + x - 1;
          n = _mm256_lddqu_si256((__m256i*)u);
          n = _mm256_add_epi8(_mm256_load_si256((__m256i*)(u + 1)), n);
          n = _mm256_add_epi8(_mm256_lddqu_si256((__m256i*)(u + 2)), n);
          u += padded_width;
          n = _mm256_add_epi8(_mm256_lddqu_si256((__m256i*)u), n);
          alive = _mm256_load_si256((__m256i*)(u + 1));
          n = _mm256_add_epi8(_mm256_lddqu_si256((__m256i*)(u + 2)), n);
          u += padded_width;
          n = _mm256_add_epi8(_mm256_lddqu_si256((__m256i*)u), n);
          n = _mm256_add_epi8(_mm256_load_si256((__m256i*)(u + 1)), n);
          n = _mm256_add_epi8(_mm256_lddqu_si256((__m256i*)(u + 2)), n);
          _mm256_store_si256((__m256i*)(my_new + y * padded_width + x),
            _mm256_or_si256(
            _mm256_and_si256(ones, _mm256_cmpeq_epi8(n, threes)),
            _mm256_and_si256(alive, _mm256_cmpeq_epi8(n, twos))));
        }
      }
      uint8_t *tmp = my_universe;
      my_universe = my_new;
      my_new = tmp;
    }
    //unpack into output array
    #pragma omp for
    for (unsigned y = Y_IN_GHOST; y < height + Y_IN_GHOST; y++) {
      for (unsigned x = X_IN_GHOST; x < width + X_IN_GHOST; x++) {
        out[(y - Y_IN_GHOST) * width + x - X_IN_GHOST] = my_universe[(y * padded_width) + x];
      }
    }
  }
  free(new);
  free(universe);
  return out;
}