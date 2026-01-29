#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cmath>
#include <unordered_map>
#include <tuple>
#include <iostream>
#include <string>
#include <array>
#include <vector>
#include <chrono>
#include <cstring>
#include <limits>
#include <cctype>
#include <cstdarg>
#include <sstream>
#include <mutex>
#include <omp.h> 

using namespace std;

/*
COMPILE:
  g++ -O3 -fopenmp sidebetsCDC.cpp -o sidebetsCDC

USAGE FORMAT:
  sidebetsCDC SIDE <52 integers>

The 52 integers are the remaining shoe counts for each *specific card* (rank+suit),
ordered as suit blocks of 13 ranks each:

  Clubs:    A 2 3 4 5 6 7 8 9 T J Q K
  Diamonds: A 2 3 4 5 6 7 8 9 T J Q K
  Hearts:   A 2 3 4 5 6 7 8 9 T J Q K
  Spades:   A 2 3 4 5 6 7 8 9 T J Q K

So the first 13 numbers are A♣..K♣, the next 13 are A♦..K♦, etc
Counts should reflect all removed cards from the deck
*/

static std::mutex engine_mutex;

//error handling for bad user input
static inline void fatalf(const char* fmt, ...) 
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static inline double qnan() 
{ 
    return std::numeric_limits<double>::quiet_NaN(); 
}

static inline void print_line_number(const char* key, double v) 
{
    if (!std::isfinite(v)) printf("%s null\n", key);
    else                   printf("%s %.12f\n", key, v);
}

static const int NUM_SUITS = 4;
static const int NUM_RANKS = 13;
static const int NUM_CARDS = 52;

//suit order must match the input order:
//0=Clubs, 1=Diamonds, 2=Hearts, 3=Spades
static inline int card_index(int suit, int rank) 
{ //rank: 0=A, 1=2, ..., 8=9, 9=T, 10=J, 11=Q, 12=K
    return suit * NUM_RANKS + rank;
}
static inline int card_suit(int idx) 
{ 
    return idx / NUM_RANKS; 
}
static inline int card_rank(int idx) 
{ 
    return idx % NUM_RANKS; 
}

//check if suit is diamond or hearts
static inline bool is_red_suit(int suit) 
{ 
    return suit == 1 || suit == 2; 
}

//convert 0-indexed rank to 1-13 value for math
static inline int rank_val_1_to_13(int rank0) 
{
    
    return rank0 + 1;
}

//sort three cards and check it they are consecutive aka a straight
static inline bool is_straight_3(const int r1, const int r2, const int r3) 
{
    int a[3] = {r1, r2, r3};
    std::sort(a, a + 3);

    //normal consecutive
    if (a[0] + 1 == a[1] && a[1] + 1 == a[2]) return true;

    // A-2-3
    if (a[0] == 1 && a[1] == 2 && a[2] == 3) return true;

    // Q-K-A (A high) -> handle wrap around logic 
    if (a[0] == 1 && a[1] == 12 && a[2] == 13) return true;

    return false;
}

struct PerfectPairsStats 
{
    double ev = 0.0;          
    double p_perfect = 0.0;
    double p_colored = 0.0;
    double p_mixed = 0.0;
    double p_lose = 0.0;
};

//simulate drawing two cards without replacement
//look at count of remaining cards to calculate exact probability of every possible pair combination
static PerfectPairsStats compute_perfect_pairs(const int cnt[NUM_CARDS]) 
{
    long long Nll = 0;
    for (int i = 0; i < NUM_CARDS; i++) Nll += cnt[i];
    const double N = (double)Nll;

    PerfectPairsStats out;
    if (Nll < 2) 
    {
        out.ev = qnan();
        return out;
    }

    const double denom = N * (N - 1.0);

    //ordered pair model for the two player cards
    //this yields the correct marginal distribution for the player's first 2 cards
    for (int a = 0; a < NUM_CARDS; a++) 
    {
        const int na = cnt[a];
        if (na <= 0) continue;
        for (int b = 0; b < NUM_CARDS; b++) 
        {
            int nb = cnt[b];
            if (a == b) nb -= 1;
            if (nb <= 0) continue;

            const double p = ((double)na * (double)nb) / denom;

            const int ra = card_rank(a), rb = card_rank(b);
            const int sa = card_suit(a), sb = card_suit(b);

            double profit = -1.0;
            if (ra == rb) 
            {
                if (sa == sb) 
                {
                    profit = 25.0; //perfect Pair 25:1
                    out.p_perfect += p;
                } 
                else if (is_red_suit(sa) == is_red_suit(sb)) 
                {
                    profit = 12.0; //coloured Pair 12:1
                    out.p_colored += p;
                } 
                else 
                {
                    profit = 6.0;  //mixed Pair 6:1
                    out.p_mixed += p;
                }
            } 
            else 
            {
                out.p_lose += p;
            }

            out.ev += p * profit;
        }
    }

    //floating error guard
    const double ps = out.p_perfect + out.p_colored + out.p_mixed + out.p_lose;
    if (fabs(ps - 1.0) > 1e-9) 
    {
        out.p_lose += (1.0 - ps);
    }

    return out;
}

struct TwentyOnePlusThreeStats 
{
    double ev = 0.0;
    double p_suited_trips = 0.0;
    double p_straight_flush = 0.0;
    double p_trips = 0.0;
    double p_straight = 0.0;
    double p_flush = 0.0;
    double p_lose = 0.0;
};

//prioritzie highest payout for any three card combination eg) if hand is flush and straight, award straight flush payout
static inline double profit_21p3(int c1, int c2, int c3, TwentyOnePlusThreeStats* acc_probs) 
{
    const int r1 = card_rank(c1), r2 = card_rank(c2), r3 = card_rank(c3);
    const int s1 = card_suit(c1), s2 = card_suit(c2), s3 = card_suit(c3);

    const bool same_rank = (r1 == r2) && (r2 == r3);
    const bool same_suit = (s1 == s2) && (s2 == s3);

    //suited Trips: all three are the exact same card type (only possible in multi-deck)
    if (same_rank && same_suit) 
    {
        if (acc_probs) acc_probs->p_suited_trips += 1.0;
        return 100.0; //100:1 payout
    }

    const int v1 = rank_val_1_to_13(r1);
    const int v2 = rank_val_1_to_13(r2);
    const int v3 = rank_val_1_to_13(r3);

    const bool straight = is_straight_3(v1, v2, v3);
    const bool flush = same_suit;

    if (straight && flush) 
    {
        if (acc_probs) acc_probs->p_straight_flush += 1.0;
        return 40.0;// 40:1 payout
    }

    if (same_rank) 
    {
        if (acc_probs) acc_probs->p_trips += 1.0;
        return 30.0; //30:1 payout
    }

    if (straight) 
    {
        if (acc_probs) acc_probs->p_straight += 1.0;
        return 10.0; //10:1 payout
    }

    if (flush) 
    {
        if (acc_probs) acc_probs->p_flush += 1.0;
        return 5.0; //5:1 payout
    }

    if (acc_probs) acc_probs->p_lose += 1.0;
    return -1.0;
}

//simulate every possible 3-card combination that can be formed from the remaining deck
static TwentyOnePlusThreeStats compute_21plus3(const int cnt[NUM_CARDS]) 
{
    long long Nll = 0;
    for (int i = 0; i < NUM_CARDS; i++) Nll += cnt[i];
    const double N = (double)Nll;

    TwentyOnePlusThreeStats out;
    if (Nll < 3) 
    {
        out.ev = qnan();
        return out;
    }

    //model three sequential draws without replacement (ordered triples)
    //since 21+3 depends only on the 3-card set, any fixed draw order works
    const double denom = N * (N - 1.0) * (N - 2.0);

    //normalize by denomination
    double raw_suited_trips = 0.0, raw_sf = 0.0, raw_trips = 0.0, raw_straight = 0.0, raw_flush = 0.0, raw_lose = 0.0;
    double ev_sum = 0.0;

    //order: c1, c2, c3
    for (int c1 = 0; c1 < NUM_CARDS; c1++) 
    {
        const int n1 = cnt[c1];
        if (n1 <= 0) continue;

        for (int c2 = 0; c2 < NUM_CARDS; c2++) 
        {
            int n2 = cnt[c2] - (c2 == c1 ? 1 : 0);
            if (n2 <= 0) continue;

            for (int c3 = 0; c3 < NUM_CARDS; c3++) 
            {
                int n3 = cnt[c3] - (c3 == c1 ? 1 : 0) - (c3 == c2 ? 1 : 0);
                if (n3 <= 0) continue;

                const double w = (double)n1 * (double)n2 * (double)n3; 
                TwentyOnePlusThreeStats tmp; 
                const double profit = profit_21p3(c1, c2, c3, &tmp);

                raw_suited_trips += w * tmp.p_suited_trips;
                raw_sf          += w * tmp.p_straight_flush;
                raw_trips       += w * tmp.p_trips;
                raw_straight    += w * tmp.p_straight;
                raw_flush       += w * tmp.p_flush;
                raw_lose        += w * tmp.p_lose;

                ev_sum += w * profit;
            }
        }
    }

    out.p_suited_trips  = raw_suited_trips / denom;
    out.p_straight_flush= raw_sf / denom;
    out.p_trips         = raw_trips / denom;
    out.p_straight      = raw_straight / denom;
    out.p_flush         = raw_flush / denom;
    out.p_lose          = raw_lose / denom;

    //normalize tiny floating mismatch
    const double ps = out.p_suited_trips + out.p_straight_flush + out.p_trips + out.p_straight + out.p_flush + out.p_lose;
    if (fabs(ps - 1.0) > 1e-9) out.p_lose += (1.0 - ps);

    out.ev = ev_sum / denom;
    return out;
}


struct RankEOR 
{
    double eor_ev[NUM_RANKS];      
    double eor_ev_pct[NUM_RANKS]; 
    bool valid[NUM_RANKS];
};

//remove one card from shoe and recalculate entire EV
static RankEOR compute_eor_rank(const int cnt_in[NUM_CARDS], double base_ev, bool for_21p3) 
{
    RankEOR out;
    for (int r = 0; r < NUM_RANKS; r++) 
    {
        out.eor_ev[r] = qnan();
        out.eor_ev_pct[r] = qnan();
        out.valid[r] = false;
    }

    //precompute EV after removing each specific card type once
    double ev_after_remove_card[NUM_CARDS];
    for (int i = 0; i < NUM_CARDS; i++) ev_after_remove_card[i] = qnan();

    //parallelize over card types if OpenMP is enabled
    #pragma omp parallel for
    for (int c = 0; c < NUM_CARDS; c++) 
    {
        if (cnt_in[c] <= 0) continue;
        int cnt2[NUM_CARDS];
        memcpy(cnt2, cnt_in, sizeof(cnt2));
        cnt2[c] -= 1;
        if (for_21p3) 
        {
            TwentyOnePlusThreeStats t = compute_21plus3(cnt2);
            ev_after_remove_card[c] = t.ev;
        } 
        else 
        {
            PerfectPairsStats p = compute_perfect_pairs(cnt2);
            ev_after_remove_card[c] = p.ev;
        }
    }

    //rank aggregate
    for (int r = 0; r < NUM_RANKS; r++) 
    {
        long long rank_total = 0;
        for (int s = 0; s < NUM_SUITS; s++) rank_total += cnt_in[card_index(s, r)];
        if (rank_total <= 0) continue;

        double e = 0.0;
        for (int s = 0; s < NUM_SUITS; s++) 
        {
            const int c = card_index(s, r);
            const int n = cnt_in[c];
            if (n <= 0) continue;

            const double pr = (double)n / (double)rank_total; 
            const double ev2 = ev_after_remove_card[c];
            if (!std::isfinite(ev2)) continue;

            e += pr * (ev2 - base_ev);
        }

        out.valid[r] = true;
        out.eor_ev[r] = e;
        out.eor_ev_pct[r] = e * 100.0;
    }

    return out;
}


static void compute_and_print_sidebets_report(const int cnt[NUM_CARDS]) 
{
    for (int i = 0; i < NUM_CARDS; i++) 
    {
        if (cnt[i] < 0) fatalf("Negative count at index %d", i);
    }

    PerfectPairsStats pp = compute_perfect_pairs(cnt);
    TwentyOnePlusThreeStats t = compute_21plus3(cnt);

    printf("Perfect Pairs\n");
    print_line_number("ev_pct", pp.ev * 100.0);
    print_line_number("p_perfect", pp.p_perfect);
    print_line_number("p_coloured", pp.p_colored);
    print_line_number("p_mixed", pp.p_mixed);
    print_line_number("p_lose", pp.p_lose);

    printf("21+3\n");
    print_line_number("ev_pct", t.ev * 100.0);
    print_line_number("p_suited_trips", t.p_suited_trips);
    print_line_number("p_straight_flush", t.p_straight_flush);
    print_line_number("p_trips", t.p_trips);
    print_line_number("p_straight", t.p_straight);
    print_line_number("p_flush", t.p_flush);
    print_line_number("p_lose", t.p_lose);

    fflush(stdout);
}

int main(int argc, char** argv) 
{
    if (argc > 1) 
    {
        const std::string cmd = argv[1];
        if (cmd == "SIDE") 
        {
            if (argc < 1 + 1 + NUM_CARDS) 
            {
                fatalf("Bad SIDE input: need 52 integers after SIDE");
            }
            int cnt[NUM_CARDS];
            memset(cnt, 0, sizeof(cnt));
            for (int i = 0; i < NUM_CARDS; i++) 
            {
                const char* s = argv[i + 2];
                char* endp = nullptr;
                long v = std::strtol(s, &endp, 10);
                if (!endp || *endp != '\0') fatalf("Bad SIDE input: non-integer '%s'", s);
                cnt[i] = (int)v;
            }
            compute_and_print_sidebets_report(cnt);
            return 0;
        }
        if (cmd == "HELP") 
        {
            printf("SIDE <52 counts>\n");
            fflush(stdout);
            return 0;
        }
        fatalf("Unknown command: %s", cmd.c_str());
    }

    std::string line;
    while (std::getline(std::cin, line)) 
    {
        if (line.empty()) continue;
        std::lock_guard<std::mutex> lk(engine_mutex);

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "SIDE") 
        {
            int cnt[NUM_CARDS];
            memset(cnt, 0, sizeof(cnt));
            for (int i = 0; i < NUM_CARDS; i++) {
                if (!(iss >> cnt[i])) fatalf("Bad SIDE input: need 52 integers after SIDE");
            }
            compute_and_print_sidebets_report(cnt);
            continue;
        }

        if (cmd == "HELP") 
        {
            printf("SIDE <52 counts>\n");
            fflush(stdout);
            continue;
        }

        printf("{\"mode\":\"error\",\"error\":\"unknown_command\"}\n");
        fflush(stdout);
    }

    return 0;
}
