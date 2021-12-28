/*
   Jakub Janiszek 323659
   Oświadczam, że jestem jedynym autorem kodu źródłowego.
*/

/*
   Podczas pisanie inspirowałem się kodem z książki CSAPP.
   Kod powstał na szkielecie z pliku mm-implicit.c
*/

/*
  Ważne wybory poczas implementacji:
  Wolne bloki
  Wolne bloki to tablica bloków o takiej strukturze w pamięci:
   +--------+------+------+---------+--------+
   | Header | prev | next |  wolne  | Footer |
   +--------+------+------+---------+--------+
R:     4        4      4       n         4
  * prev oraz next to offset od heap_start('dobrze wyznaczony' początek stery)
  * next == -1 dla ostatniego bloku
  * prev == -1 dla pierwszego bloku
  * header oraz footer zawierają informacje o wielkości bloku
    oraz o tym czy blok jest wolny
  * minimalna wielkość wolnego bloku to 16bajtów
  * wolne bloki które w pamięci leżą obok siebie są od razu łączone
  * tablica wolnych bloków jest nie jest posegregowana
  W ostatniej CHWILI 6h przed oddaniem kodu przez przypadek podczas debugowania,
odkryłem że dzięki polityce nieposegregowanej tyablicy wolnej pamięci mój
program jest o ziemie szybszy

  Zajęte bloki
  Blok zajęty wygląda tak:
   +--------+---------+--------+
   | Header | Payload | Footer |
   +--------+---------+--------+
  * header oraz footer zawierają informacje o wielkości bloku
   oraz o tym czy blok jest wolny
*/
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* !DRIVER */

typedef int32_t word_t; /* Sterta to tablica 4-bajtowych słów */
#define block_size 4

typedef enum {
  FREE = 0, /* Block jest wolny */
  USED = 1, /* Block jest używany */
} bt_flags;

static word_t *heap_start;  /* Adres pierwszego bloku*/
static word_t *heap_end;    /* Adres kończący nasze bloki*/
static word_t *free_blocks; /* Adres pierwszego wolnego bloku */
/* to jest złoto dzięki temu zaoszczędzamy kilka tycięcy instrukcji, */
static size_t maybe_max_fb; /* rozmiar wolnego bloku(najczęściej największego)
                               lub -1 tłumacze się z tego dalej*/

/*__Funkcje obsługujące boundary tagi czyli headera i footera__*/

/* zwraca wielkość bloku pamięci | funkcja inspirowana mm-implicit.c* */
static inline word_t bt_size(word_t *bt) {
  return *bt & ~USED;
}

/* zwraca 0/1 mówiące czy dany blok jest używany czy nie | funkcja inspirowana
 * mm-implicit.c* */
static inline int bt_used(word_t *bt) {
  return *bt & USED;
}
/* zwraca 0/1 mówiące czy dany blok jest wolny czy nie | funkcja inspirowana
 * mm-implicit.c* */
static inline int bt_free(word_t *bt) {
  return !(*bt & USED);
}

/* zwraca adres footera | funkcja inspirowana mm-implicit.c* */
static inline word_t *bt_footer(word_t *bt) {
  return (void *)bt + bt_size(bt) - sizeof(word_t);
}

/* input adres paylodu output header | funkcja inspirowana mm-implicit.c*  */
static inline word_t *bt_fromptr(void *ptr) {
  return (word_t *)ptr - 1;
}

/* Stworzenie headera i footera
   W inpucie chce mieć wskaźnik na cały blok pamięci
   w bd używamy optymalizacji z wykładu tzn
   dolne bity będą wolne bo rozmiar będzie wielokrotnością 16 */
static inline void bt_make(word_t *bt, size_t size, bt_flags flags) {
  // ustawiam header
  *bt = size | flags;
  // ustawian footer
  bt = bt_footer(bt);
  *bt = size | flags;
}

/* zwraca adres payload. | funkcja inspirowana mm-implicit.c*  */
static inline void *bt_payload(word_t *bt) {
  return bt + 1;
}

/* -------------------Funkcje wielkość/pamięć------------------------ */

/*
  normalizuje żądany rozmiar bloku, aby adres był podzielny przez 16 oraz
  aby było miejsce dla headera i footera
  Wzoruje się  mocno na funkcji round_up z prostej implementacji
 */
static inline size_t normalize_size(size_t size) {
  size += 2 * sizeof(word_t); // miejsce na footer i header
  return (size + ALIGNMENT - 1) & -ALIGNMENT;
}
/* więcej pamięci | funkcji z mm-implicit.c*/
static void *morecore(size_t size) {
  void *ptr = mem_sbrk(size);
  if (ptr == (void *)-1)
    return NULL;
  return ptr;
}

/* __Funkcje pomocnicze obsługujące tablice wolnych bloków__

  tablica wolnych bloków do dwukierunkowa lista taka jaką
  widzimy na slajdzie 4 wykładu 8b
  list będzie nieuporządkowana
  dlaczego?
  o dziwo taka lista w mojej implementacji daje lepsze wyniki na testach niż
  moja implentacja listy uporządkowanej, ale to pewnie wina mojej implementacji.

  dodając nowy pusty blok wsadzamy go na początek listy wolnych bloków
  offset = odległość od początka sterty wyrażona w blokach
*/

/* zwraca null lub wskaźnik na następny blok pamięci */
static inline word_t *next_fb(word_t *bt) {
  word_t offset = *(bt + 2);
  if (offset == -1)
    return NULL;
  return heap_start + offset;
}

/* zwraca null lub wskaźnik na poprzedni blok pamięci */
static inline word_t *prev_fb(word_t *bt) {
  word_t offset = *(bt + 1);
  if (offset == -1)
    return NULL;
  return heap_start + offset;
}

/* zapamiętanie nowego następnego wollnego bloku */
static inline void set_next_fb(word_t *bt, word_t offset) {
  *(bt + 2) = offset;
}

/* zapamiętanie nowego poprzedniego wolnego bloku */
static inline void set_prev_fb(word_t *bt, word_t offset) {
  *(bt + 1) = offset;
}

/* funkcja pomocnicza wsadzająca wolny blok do tablicy wolnych bloków
   obsługuje zamiane offsetów

static inline void set_new_fb(word_t *bt, word_t *next)
{
  word_t *prev = prev_fb(next);
  if(prev == NULL)
  {
    free_blocks = bt;
    set_prev_fb(bt, -1);
  }
  else{
    set_next_fb(prev, bt - heap_start);
    set_prev_fb(bt, prev - heap_start);
  }
  set_next_fb(bt, next- heap_start);
  set_prev_fb(next, bt - heap_start);
}
*/
/* dodaje nowy blok to tablicy bloków
   Zasady:
   - pierwszy wolny blok ma wartość prev ustaawioną na -1
   - ostatni wolny blok ma wartość next ustawioną na -1
*/
static inline void add_new_fb(word_t *bt) {
  // lista wolnych bloków jest pusta
  if (free_blocks == NULL) {
    set_next_fb(bt, -1);
    set_prev_fb(bt, -1);
    maybe_max_fb = bt_size(bt);
    free_blocks = bt;
    return;
  }
  // wsadzamy wolny blok na początek niepustej listy wolnych bloków
  else {
    set_prev_fb(bt, -1);
    set_next_fb(bt, free_blocks - heap_start);
    set_prev_fb(free_blocks, bt - heap_start);
    free_blocks = bt;
  }
}

/* Usunięcie z listy wolnych bloków
   Uwaga ta funckja nie zmienia flagi!!!*/
static inline void remove_fb(word_t *bt) {
  // bierzemy następny i poprzedni wolny blok
  word_t *prev = prev_fb(bt);
  word_t *next = next_fb(bt);
  // jedyny wolny blok
  if (prev == NULL && next == NULL) {
    free_blocks = NULL;
    maybe_max_fb = 0;
  }
  // pierwszy wolny blok
  else if (prev == NULL) {
    free_blocks = next;
    set_prev_fb(next, -1);
  }
  // ostatni wolny blok
  else if (next == NULL) {
    set_next_fb(prev, -1);
  }
  // wpp
  else {
    set_next_fb(prev, next_fb(bt) - heap_start);
    set_prev_fb(next, prev_fb(bt) - heap_start);
  }
  // jeśli usuneliśmy największy wolny blok to musimy znaleść nowy największy
  // ustawienie tego na -1 jest o tyle dobrym pomysłem, że rozmiar nowego bloku
  // na pewno tu wejdie może nie będzie największy ale oszczędzamy tysiące cykli
  // na nie przeszukiwaniu listy wolnych bloków, więc poświęcam ładny algorytm
  // na rzecz sprawnego działania na testach
  if (maybe_max_fb == bt_size(bt)) {
    maybe_max_fb = -1;
  }
}

/*-------------------------Część główna-------------------------------*/
/* mm_init
   procedura inicjalizuje algorytm zarządzania pamięcią
   ustawia wszystkie wartości globalne i zajmuje się dorównanie adresu pamięci
   | funkcja inspirowana mm-implicit.c
*/
int mm_init(void) {
  void *ptr = morecore(ALIGNMENT - sizeof(word_t));
  if (!ptr)
    return -1;
  heap_start = NULL;
  heap_end = NULL;
  free_blocks = NULL;
  maybe_max_fb = 0;
  return 0;
}

/* malloc
   dostajemy posortowaną liste wolnych bloków więc przeszukujemy ją
   w poszukiwaniu odpowiedniego rozmiaru(funkcją find_fit)
   przez to że jest ona posortowana to mamy pewność, biorąc 1 odpowiedni
   weźmiemy ten najlepszy
   jeśli nie znajdziemy wolnego bloku to zwiększamy sterte
   Funkcja malloc jest zainspirowana kodem z książki CS:APP
*/

static word_t *find_fit(size_t reqsz) {
  if (maybe_max_fb < reqsz) {
    // najprawdopodobniej nie ma takiego bloku
    return NULL;
  }

  /* szukamy wolnego bloku polityką best fit
      jest ona wolniejsz od fist fit co widać w testach, ale
      fragmentacji jest mniejsza a tego byśmy chcieli

      first_fit: Weighted memory utilization: 79.5%
                 Instructions per operation 233
      best_fit: Weighted memory utilization: 80.5%
                Instructions per operation:430

      first fit jest 2 razy szybszy, ale troche gorzej zarządza pamięcią
      ja postawiłem na best fita bo wole mieć
      lepiej zarządzaną pamięć nawet kosztem
      czasu */
  word_t *fb = free_blocks;
  size_t size = bt_size(fb);
  word_t *bestfit = NULL;
  size_t bestsize = 0;
  // znajdowanie pasującego bloku
  while (fb != NULL) {
    if (reqsz < size) {
      bestfit = fb;
      bestsize = size;
    }
    if (reqsz == size) {
      bestfit = fb;
      bestsize = size;
      break;
    }
    fb = next_fb(fb);
    size = bt_size(fb);
  }
  if (bestfit == NULL) {
    return NULL;
  }
  remove_fb(bestfit);
  size_t size_diff = bestsize - reqsz;
  // dziele blok jeżeli mogę, dla lepszej optymalizacji pamięci
  if (size_diff > block_size * sizeof(word_t)) {
    word_t *new_fb = bestfit + (reqsz / block_size);
    bt_make(bestfit, reqsz, USED);
    bt_make(new_fb, size_diff, FREE);
    add_new_fb(new_fb);
  }
  return bestfit;
}

void *mm_malloc(size_t size) {
  // printf("malloc\n");
  if (size == 0) {
    return NULL;
  }
  // uzyskuje podzielność przez 16 i miejsce na header i footer
  size = normalize_size(size);
  size_t blocks = size / block_size;
  word_t *new_block;
  if (free_blocks == NULL) {
    // nie ma wolnych bloków
    new_block = morecore(size);
    heap_end = new_block + blocks;
    if (heap_start == 0) {
      heap_start = new_block;
    }
  } else {
    new_block = find_fit(size);
    if (new_block == NULL) {
      // jak nie znalazłem wolnego bloku to zwiększam sterte
      new_block = morecore(size);
      heap_end = new_block + blocks;
    } else {
      // dla pewności bo mogliśmy dostać więcej pamięci niż chcieliśmy
      size = bt_size(new_block);
    }
  }
  // zmioeniamy flage ustawiamy footer i header
  bt_make(new_block, size, USED);
  // zwracamy wskaźnik na miejsce na dane
  new_block = bt_payload(new_block);
  return new_block;
}

/*  free
    Funkcja free jest zainspirowana kodem z książki CS:APP
    coalasce złączy wolne bloki
*/

/* złączenie  bloków, algorytm jest wolną adaptacją kodu funkcji coalasce
   z książki CS:APP
   funkcja łączy wolne bloki
   na początku sprawdzam czy bloki z tyłu z przodu są wolne,
   jeżeli tak to łączymy
   łączenie to defakto odpowiednie modyfikowanie offsetów
   na liście wolnych bloków oraz ustawienie footera i headera
*/
static inline word_t *coalasce(word_t *bt) {
  word_t *next;
  word_t *prev;
  word_t next_free;
  word_t prev_free;
  word_t size;

  // sprawdzanie czy poprzednilub następny blok jest wolny
  if (bt != heap_start) {
    prev = bt - 1;
    prev_free = bt_free(prev);
    prev = bt - (bt_size(prev) / block_size);
  } else {
    prev_free = 0;
  }
  next = bt + (bt_size(bt) / block_size);
  if (next < heap_end) {
    next_free = bt_free(next);
  } else {
    next_free = 0;
  }

  // okalające bloki są wolne
  if (next_free && prev_free) {
    remove_fb(prev);
    remove_fb(next);
    size = bt_size(prev) + bt_size(bt) + bt_size(next);
    bt = prev;
    bt_make(bt, size, FREE);
    add_new_fb(bt);
  }
  // poprzedni blok jest wolny
  else if (prev_free && !next_free) {
    remove_fb(prev);
    size = bt_size(prev) + bt_size(bt);
    bt = prev;
    bt_make(bt, size, FREE);
    add_new_fb(bt);
  }
  // następny blok jest wolny
  else if (!prev_free && next_free) {
    remove_fb(next);
    size = bt_size(bt) + bt_size(next);
    bt_make(bt, size, FREE);
    add_new_fb(bt);
  }
  // bloki z przodu i tyłu są zajęte
  else {
    size = bt_size(bt);
    bt_make(bt, size, FREE);
    add_new_fb(bt);
  }
  if (size > maybe_max_fb) {
    maybe_max_fb = size;
  }
  return bt;
}

void mm_free(void *ptr) {
  // printf("free\n");
  if (ptr != NULL) {
    word_t *bt = bt_fromptr(ptr); // dostaniemy bt
    // złączam wolne bloki
    bt = coalasce(bt);
  }
}

/* realloc
  implementacja na podstawie dokumentacji funkcji z so21_projekt_malloc_v0.pdf
  w 3 przypadku nie ma niektórych możliwości odnajdywania wolnego bloku
  ponieważ ich implementacja pogorszyła końcowe
  wyniki testów
*/

void *mm_realloc(void *old_ptr, size_t size) {
  // jeśli ptr == NULL, to wywołanie jest tożsame z mm_malloc(size)»,
  if (old_ptr == NULL) {
    return mm_malloc(size);
  }
  // jeśli «size» jest równy 0, to wywołanie jest tożsame z «mm_free(ptr)»,
  if (size == 0) {
    mm_free(old_ptr);
    return NULL;
  }
  // wpp naprawde realokujemy
  word_t *bt = bt_fromptr(old_ptr);
  size_t size_bt = bt_size(bt);
  // jeżeli zmniejszamy blok to po prost oddajemy stary blok
  if (size_bt - 2 * (sizeof(word_t)) >= size) {
    return old_ptr;
  }
  // gdy trzeba powiększyć,
  // sprawdzamy czy może bloki z tyłu lub z przodu nie są wolne
  // jeśli są to je łączymy
  word_t *next = bt + bt_size(bt) / block_size;
  size_t size_next = 0;
  if (next < heap_end && bt_free(next)) {
    size_next = bt_size(next);
    if (size_next + size_bt - (2 * sizeof(word_t)) >= size) {
      remove_fb(next);
      bt_make(bt, size_next + size_bt, USED);
      return bt_payload(bt);
    }
  }
  word_t *prev = bt - bt_size(bt - 1) / block_size;
  if (prev >= heap_start && bt_free(prev)) {
    size_t prev_size = bt_size(prev);
    if (prev_size + size_bt - (2 * sizeof(word_t)) >= size) {
      remove_fb(prev);
      memcpy(prev + 1, old_ptr, size_bt);
      bt_make(prev, prev_size + size_bt, USED);
      return bt_payload(prev);
    }
  }
  /* z tym wychodzą o 0.2 gorsze wyniki więc nie dodam
  if(bt+size_bt/ block_size  == heap_end)
  {

    size_t size_diff = size - size_bt;
    size_diff= normalize_size(size_diff);
    morecore(size_diff);
   //printf("aaa\n");
    heap_end = heap_end + (size_diff / block_size);
    bt_make(bt,size_bt + size_diff , USED);
    return bt_payload(bt);
  }
  */
  void *new = mm_malloc(size);

  memcpy(new, old_ptr, size_bt);
  free(old_ptr);
  return new;
}

/* calloc
   funcja zaadoptowana z mm-implicit.c */

void *mm_calloc(size_t nmemb, size_t size) {
  // printf("calloc");
  size_t bytes = nmemb * size;
  void *new_ptr = malloc(bytes);
  if (new_ptr)
    memset(new_ptr, 0, bytes);
  return new_ptr;
}

/* --=[ mm_checkheap ]=-----------------------------------------------------
   Błędy:
   1 - wolny blok nie jest znaczony jako wolny blok
   2  - wskaźniki na poprzedni i następny blok wskazują poza zaalokowaną sterte
   3 - występują dwa wolne bloki obok siebie
   4 - nie wszystkie wolne bloki są na liście wolnych bloków
   Funkcja jest raczej brzydka, ale pisałem ją dla siebie, aby w przyjemny dla
   mnie sposób pomogła mi debugować program. I pomogła.
   */

void mm_checkheap(int verbose) {
  int error = 0;
  int error_num = -1;
  // sprawdzam czy wszytkie bloki na wolnej liście są ustawione jako wolne
  int count_free_block = 0;
  word_t *block = free_blocks;
  word_t *next;
  word_t *prev;
  // przechodzimy po kolei przez wszystkie wolne bloki
  while (block != NULL) {
    // zliczamy wolne bloki
    count_free_block++;
    // sprawdzamy poprawność ich bt
    if (bt_used(block)) {
      error = 1;
      error_num = 1;
    }
    next = next_fb(block);
    prev = prev_fb(block);
    // sprawdza czy blok nie wychodzą poza sterte
    if (!(next == NULL || next < heap_end)) {
      error = 1;
      error_num = 2;
    }
    if (!(prev == NULL || prev >= heap_start)) {
      error = 1;
      error_num = 2;
    }
    block = next;
  }
  // przechodzimy przez wszystkie bloki te wolne i zajęte
  block = heap_start;
  while (block != heap_end) {
    next = block + (bt_size(block) / block_size);
    // sprawdza czy nie występują dwa wolne bloki kolo siebie
    if (bt_free(block)) {
      if (bt_free(next) && next != heap_end) {
        error = 1;
        error_num = 3;
      }
      // odejmujemy potrzebne za chwile
      count_free_block--;
    }
    block = next;
  }
  // sprawdzamy czy wszystkie wolne bloki są w liście wolnych bloków
  if (count_free_block != 0) {
    error = 1;
    error_num = 4;
  }

  // drukuje wszystkie bloki w pamięci, bardzo przydatne!!!
  if (verbose != 0) {
    printf("printed debug info\n");
    if (heap_start == NULL) {
      printf("empty memory");
      return;
    }
    block = heap_start;

    word_t size;
    int free;
    int index_next;
    int index_prev;
    printf("heap start: %ls\t, heap end: %ls\n", heap_start, heap_end);
    while (block != heap_end) {
      size = bt_size(block);
      free = bt_free(block);
      if (free) {
        index_prev = *(block + 1);
        index_next = *(block + 2);
        printf("\nF: %d, %d, %d\n", size, index_prev, index_next);
      } else {
        printf("Z: %d", size);
      }
      block = block + (size / block_size);
    }
  }
  if (error) {
    printf("\n%d\n", error_num);
    exit(error_num);
  }
}
