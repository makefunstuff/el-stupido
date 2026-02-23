#include "el-stupido.h"

void main_body() {
    for (int i := 2..=50) {
        bool is_prime = true;
        for (int j := 1..=i) {
            if ((j != 1) && (j != i)) {
                int q = i / j;
                if (((q * j) == i) && (j < q)) {
                    is_prime = false;
                }
            }
        }

        if (is_prime) {
            print(i);
        }
    }
}