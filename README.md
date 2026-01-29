Composition Dependent Calculator For Pragmatic Play live blackjack table 21+3 and Perfect Pair side bets:

Perfect Pair Payouts:
```
Perfect pairs 25:1
Coloured pairs: 12:1
Mixed Pair 6:1
```

21+3 Payouts:
```
Suited Trips 100:1
Straight Flush 40:1
Three of a kind 30:1
Straight 10:1
Flush 5:1
```

To compile:
```
g++ -O3 -fopenmp sidebetsCDC.cpp -o sidebetsCDC
```

USAGE:

1) Calculate EV of perfect pair and 21+3 side bets given a specific deck composition

```
sidebetsCDC SIDE <52 integers>

The 52 integers are the remaining shoe counts for each *specific card* (rank+suit),
ordered as suit blocks of 13 ranks each:

  Clubs:    A 2 3 4 5 6 7 8 9 T J Q K
  Diamonds: A 2 3 4 5 6 7 8 9 T J Q K
  Hearts:   A 2 3 4 5 6 7 8 9 T J Q K
  Spades:   A 2 3 4 5 6 7 8 9 T J Q K
```

eg) Calculate EV of PP and 21+3 sidebets given a full 8 deck shoe

```
sidebets SIDE 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8
```

TIME COMPLEXITY = O(1)

O(C^3), where C is the number of distinct cards, which is fixed to 52, so effectively O(52^3) which is constant time 

