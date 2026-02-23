#ifndef NORMALIZE_H
#define NORMALIZE_H

/* Intent normalizer.
   Runs after preprocess/codebook and before parsing.
   Rewrites common LLM-friendly tokens into canonical core forms. */
char *normalize_source(const char *src);

#endif /* NORMALIZE_H */
