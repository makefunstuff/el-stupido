#ifndef CODEBOOK_H
#define CODEBOOK_H

/* Codebook expander — runs after preprocess(), before parser.
   Scans for 📥 <codebook> declarations and domain-specific emoji
   directives, expands them to full .es source code.

   Hybrid model: thick defaults (full implementation), thin overrides
   (user supplies { } block to replace handler body). */

char *codebook_expand(const char *src);

#endif /* CODEBOOK_H */
