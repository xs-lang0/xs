#ifndef XS_UTILS_H
#define XS_UTILS_H

#include <string.h>

static inline int xs_edit_distance(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 32 || lb > 32) return 999;
    int dp[33][33];
    for (int i = 0; i <= la; i++) dp[i][0] = i;
    for (int j = 0; j <= lb; j++) dp[0][j] = j;
    for (int i = 1; i <= la; i++) {
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            dp[i][j] = dp[i-1][j] + 1;
            if (dp[i][j-1] + 1 < dp[i][j]) dp[i][j] = dp[i][j-1] + 1;
            if (dp[i-1][j-1] + cost < dp[i][j]) dp[i][j] = dp[i-1][j-1] + cost;
        }
    }
    return dp[la][lb];
}

#endif
