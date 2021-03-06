 

/*----------------------------------------------------------------------+
Adam's Parallel chess program.
Created 1 April 2015
Updated: May 2015
This code takes Marcel's Simple Chess Program and improves it by adding
Cilk.
+----------------------------------------------------------------------*/


/*----------------------------------------------------------------------+
 |                                                                      |
 |              mscp.c - Marcel's Simple Chess Program                  |
 |                                                                      |
 +----------------------------------------------------------------------+
 |
 | Author:      Marcel van Kervinck <marcelk@bitpit.net>
 | Creation:    11-Jun-1998
 | Last update: 14-Dec-2003
 | Description: Simple chess playing program
 |
 +----------------------------------------------------------------------+
 |    Copyright (C)1998-2003 Marcel van Kervinck                        |
 |    This program is distributed under the GNU General Public License. |
 |    See file COPYING or http://bitpit.net/mscp/ for details.          |
 +----------------------------------------------------------------------*/

char mscp_c_rcsid[] = "@(#)$Id: mscp.c,v 1.18 2003/12/14 15:12:12 marcelk Exp $";

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "cycle_timer.h"
#include <cilk/cilk.h>
#include <pthread.h>

typedef unsigned char byte;
#define INF 32000
#define MAX(a,b) ((a)>(b) ? (a) : (b))
#define MIN(a,b) ((a)<(b) ? (a) : (b))

#define xisspace(c) isspace((int)(c)) /* Prevent warnings on crappy Solaris */
#define xisalpha(c) isalpha((int)(c))

/*----------------------------------------------------------------------+
 |      data structures                                                 |
 +----------------------------------------------------------------------*/

enum {                  /* 64 squares */
        A1, A2, A3, A4, A5, A6, A7, A8,
        B1, B2, B3, B4, B5, B6, B7, B8,
        C1, C2, C3, C4, C5, C6, C7, C8,
        D1, D2, D3, D4, D5, D6, D7, D8,
        E1, E2, E3, E4, E5, E6, E7, E8,
        F1, F2, F3, F4, F5, F6, F7, F8,
        G1, G2, G3, G4, G5, G6, G7, G8,
        H1, H2, H3, H4, H5, H6, H7, H8,
        CASTLE,         /* Castling rights */
        EP,             /* En-passant square */
        LAST            /* Ply number of last capture or pawn push */
};
static byte board[64+3]; /* Board state */

static int ply;         /* Number of half-moves made in game and search */
#define WTM (~ply & 1)  /* White-to-move predicate */

static byte castle[64]; /* Which pieces may participate in castling */
#define CASTLE_WHITE_KING  1
#define CASTLE_WHITE_QUEEN 2
#define CASTLE_BLACK_KING  4
#define CASTLE_BLACK_QUEEN 8

static int computer[2]; /* Which side is played by the computer */
static int xboard_mode; /* XBoard protocol, surpresses prompt */

static unsigned long nodes; /* Node counter */

struct side {           /* Attacks */
        byte attack[64];
        int king;
        byte pawns[10];
};
static struct side white, black, *friend, *enemy;

static unsigned short history[64*64]; /* History-move heuristic counters */

static signed char undo_stack[6*1024], *undo_sp; /* Move undo administration */
static unsigned long hash_stack[1024]; /* History of hashes, for repetition */

#define RANDOM_DEPTH 2
#define FINAL_DEPTH 7
static int maxdepth = RANDOM_DEPTH;                /* Maximum search depth */
static int parallel_code=0;
#define RANDOM_COUNTDOWN_START 0
#define GAME_LENGTH 12
#define CILK_THRESHOLD 5
#define WORK_STEAL_A 0
#define WORK_STEAL_B 0
static float TIME_LIMIT=1.1;
static int global_depth;
static int random_countdown=RANDOM_COUNTDOWN_START;
static int total_nodes_visited=0;
static const char late_path[]= "/home/akavka/minimax/";


/* Constants for static move ordering (pre-scores) */
#define PRESCORE_EQUAL       (10U<<9)
#define PRESCORE_HASHMOVE    (3U<<14)
#define PRESCORE_KILLERMOVE  (2U<<14)
#define PRESCORE_COUNTERMOVE (1U<<14)

static const int prescore_piece_value[] = {
        0,
        0, 9<<9, 5<<9, 3<<9, 3<<9, 1<<9,
        0, 9<<9, 5<<9, 3<<9, 3<<9, 1<<9,
};

struct move {
        short move;
        unsigned short prescore;
};

static struct move move_stack[1024], *move_sp; /* History of moves */

static int piece_square[12][64];        /* Position evaluation tables */
static unsigned long zobrist[12][64];   /* Hash-key construction */

/*
 *  CORE is the number of entries in the opening book or transposition table.
 *  It must be power of two. MSCP is very simple and optimized for 4 ply,
 *  so I don't want to waste space on a large table. This also makes MSCP
 *  fit well on 8bit machines.
 */
#define CORE (2048)
static long booksize;                   /* Number of opening book entries */

typedef struct ball {
  struct side white;
  struct side black;
  struct side *enemy, *friend;
  int ply;
  unsigned short caps;
  struct move *move_sp;
  signed char *undo_sp;
  int nodes_visited;
  struct move* copy_move_stack;
}ball;


/* Transposition table and opening book share the same memory */
static union {
        struct tt {                     /* Transposition table entry */
                unsigned short hash;    /* - Identifies position */ 
                short move;             /* - Best recorded move */
                short score;            /* - Score */
                char flag;              /* - How to interpret score */
                char depth;             /* - Remaining search depth */
        } tt[CORE];
        struct bk {                     /* Opening book entry */
                unsigned long hash;     /* - Identifies position */
                short move;             /* - Move for this position */
                unsigned short count;   /* - Frequency */
        } bk[CORE];
} core;

#define TTABLE (core.tt)
#define BOOK (core.bk)

/*----------------------------------------------------------------------+
 |      chess defines                                                   |
 +----------------------------------------------------------------------*/

enum {
        EMPTY,
        WHITE_KING, WHITE_QUEEN, WHITE_ROOK,
        WHITE_BISHOP, WHITE_KNIGHT, WHITE_PAWN,
        BLACK_KING, BLACK_QUEEN, BLACK_ROOK,
        BLACK_BISHOP, BLACK_KNIGHT, BLACK_PAWN
};
#define PIECE_COLOR(pc) ((pc) < BLACK_KING) /* White or black */

#define DIR_N (+1)              /* Offset to step one square up (north) */
#define DIR_E (+8)              /* Offset to step one square right (east) */

enum { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H };
enum { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8 };

#define RANK2CHAR(r)            ('1'+(r))               /* rank to text */
#define CHAR2RANK(c)            ((c)-'1')               /* text to rank */
#define FILE2CHAR(f)            ('a'+(f))               /* file to text */
#define CHAR2FILE(c)            ((c)-'a')               /* text to file */
#define PIECE2CHAR(p)           ("-KQRBNPkqrbnp"[p])    /* piece to text */

#define F(square)               ((square) >> 3)         /* file */
#define R(square)               ((square) & 7)          /* rank */
#define SQ(f,r)                 (((f) << 3) | (r))      /* compose square */
#define FLIP(square)            ((square)^7)            /* flip board */
#define MOVE(fr,to)             (((fr) << 6) | (to))    /* compose move */
#define FR(move)                (((move) & 07700) >> 6) /* from square */
#define TO(move)                ((move) & 00077)        /* target square */
/*SIMPLE I added this*/
#define PWTM(x)                 (~x & 1)                /* WTM with argument*/
#define SPECIAL                 (1<<12)                 /* for special moves */

struct command {
        char *name;
        void (*cmd)(char*);
        char *help;
};

/* attacks */
#define ATKB_NORTH              0
#define ATKB_NORTHEAST          1
#define ATKB_EAST               2
#define ATKB_SOUTHEAST          3
#define ATKB_SOUTH              4
#define ATKB_SOUTHWEST          5
#define ATKB_WEST               6
#define ATKB_NORTHWEST          7

#define ATK_NORTH               (1 << ATKB_NORTH)
#define ATK_NORTHEAST           (1 << ATKB_NORTHEAST)
#define ATK_EAST                (1 << ATKB_EAST)
#define ATK_SOUTHEAST           (1 << ATKB_SOUTHEAST)
#define ATK_SOUTH               (1 << ATKB_SOUTH)
#define ATK_SOUTHWEST           (1 << ATKB_SOUTHWEST)
#define ATK_WEST                (1 << ATKB_WEST)
#define ATK_NORTHWEST           (1 << ATKB_NORTHWEST)

#define ATK_ORTHOGONAL          ( ATK_NORTH | ATK_SOUTH | \
                                  ATK_WEST  | ATK_EAST  )
#define ATK_DIAGONAL            ( ATK_NORTHEAST | ATK_NORTHWEST | \
                                  ATK_SOUTHEAST | ATK_SOUTHWEST )
#define ATK_SLIDER              ( ATK_ORTHOGONAL | ATK_DIAGONAL )

static signed char king_step[129];      /* Offsets for king moves */
static signed char knight_jump[129];    /* Offsets for knight jumps */ 

/* 8 bits per square representing which directions a king can walk to */
static const byte king_dirs[64] = {
          7,  31,  31,  31,  31,  31,  31,  28,
        199, 255, 255, 255, 255, 255, 255, 124,
        199, 255, 255, 255, 255, 255, 255, 124,
        199, 255, 255, 255, 255, 255, 255, 124,
        199, 255, 255, 255, 255, 255, 255, 124,
        199, 255, 255, 255, 255, 255, 255, 124,
        199, 255, 255, 255, 255, 255, 255, 124,
        193, 241, 241, 241, 241, 241, 241, 112,
};

/* 8 bits per square representing which directions a knight can jump to */
static const byte knight_dirs[64] = {
         6,  14,  46,  46,  46,  46,  44,  40,
         7,  15,  63,  63,  63,  63,  60,  56,
        71, 207, 255, 255, 255, 255, 252, 184,
        71, 207, 255, 255, 255, 255, 252, 184,
        71, 207, 255, 255, 255, 255, 252, 184,
        71, 207, 255, 255, 255, 255, 252, 184,
        67, 195, 243, 243, 243, 243, 240, 176,
        65, 193, 209, 209, 209, 209, 208, 144,
};

/*----------------------------------------------------------------------+
 |      simple random generator                                         |
 +----------------------------------------------------------------------*/

static long rnd_seed = 1;

static long rnd(void)
{
        long r = rnd_seed;

        r = 16807 * (r % 127773L) - 2836 * (r / 127773L);
        if (r < 0) r += 0x7fffffffL;

        return rnd_seed = r;
}

/*----------------------------------------------------------------------+
 |      I/O functions                                                   |
 +----------------------------------------------------------------------*/

static void print_square(int square)
{
        putchar(FILE2CHAR(F(square)));
        putchar(RANK2CHAR(R(square)));
}

static void print_move_long(int move)
{
        print_square(FR(move));
        print_square(TO(move));
        if (move >= SPECIAL) {
                if ((board[FR(move)] == WHITE_PAWN && R(TO(move)) == RANK_8)
                ||  (board[FR(move)] == BLACK_PAWN && R(TO(move)) == RANK_1)) {
                        putchar('=');
                        putchar("QRBN"[move >> 13]);
                }
        }
}

static void print_board(void)
{
        int file, rank;

        for (rank=RANK_8; rank>=RANK_1; rank--) {
                printf("%d ", 1+rank);
                for (file=FILE_A; file<=FILE_H; file++) {
                        putchar(' ');
                        putchar(PIECE2CHAR(board[SQ(file,rank)]));
                }
                putchar('\n');
        }
        printf("   a b c d e f g h\n%d. %s to move. %s%s%s%s ",
                1+ply/2, WTM ? "White" : "Black",
                board[CASTLE] & CASTLE_WHITE_KING ? "K" : "",
                board[CASTLE] & CASTLE_WHITE_QUEEN ? "Q" : "",
                board[CASTLE] & CASTLE_BLACK_KING ? "k" : "",
                board[CASTLE] & CASTLE_BLACK_QUEEN ? "q" : ""
        );
        if (board[EP]) print_square(board[EP]);
        putchar('\n');
}

static int readline(char *line, int size, FILE *fp)
{
        int c;
        int pos = 0;

        for (;;) {
                errno = 0;
                c = fgetc(fp);
                if (c == EOF) {
                        if (!errno) return -1;
                        printf("error: %s\n", strerror(errno));
                        errno = 0;
                        continue;
                }
                if (c == '\n') {
                        break;
                }
                if (pos < size-1) {
                        line[pos++] = c;
                }
        }
        line[pos] = 0;
        return pos;
}

/*----------------------------------------------------------------------+
 |      transposition tables                                            |
 +----------------------------------------------------------------------*/

/* Compute 32-bit Zobrist hash key. Normally 32 bits this is too small,
   but for MSCP's small searches it is OK */
static unsigned long compute_hash(void)
{
        unsigned long hash = 0;
        int sq;

        for (sq=0; sq<64; sq++) {
                if (board[sq] != EMPTY) {
                        hash ^= zobrist[board[sq]-1][sq];
                }
        }
        return hash ^ WTM;
}

/*----------------------------------------------------------------------+
 |      position                                                        |
 +----------------------------------------------------------------------*/

static void reset(void)
{
        move_sp = move_stack;
        undo_sp = undo_stack;
        hash_stack[ply] = compute_hash();
}

static void setup_board(char *fen)
{
        int file=FILE_A, rank=RANK_8;

        while (xisspace(*fen)) fen++;

        memset(board, 0, sizeof board);

        while (rank>RANK_1 || file<=FILE_H) {
                int piece = EMPTY;
                int count = 1;

                switch (*fen) {
                case 'K': piece = WHITE_KING; break;
                case 'Q': piece = WHITE_QUEEN; break;
                case 'R': piece = WHITE_ROOK; break;
                case 'B': piece = WHITE_BISHOP; break;
                case 'N': piece = WHITE_KNIGHT; break;
                case 'P': piece = WHITE_PAWN; break;
                case 'k': piece = BLACK_KING; break;
                case 'r': piece = BLACK_ROOK; break;
                case 'q': piece = BLACK_QUEEN; break;
                case 'b': piece = BLACK_BISHOP; break;
                case 'n': piece = BLACK_KNIGHT; break;
                case 'p': piece = BLACK_PAWN; break;
                case '/': rank -= 1; file = FILE_A; fen++; continue;
                case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8':
                        count = *fen - '0';
                        break;
                default:
                        puts("fen error\n");
                        return;
                }
                do {
                        board[SQ(file,rank)] = piece;
                        file++;
                } while (--count);
                fen++;
        }
        ply = (fen[1] == 'b');
        board[LAST] = ply;
        fen += 2;

        while (*fen) {
                switch (*fen) {
                case 'K': board[CASTLE] |= CASTLE_WHITE_KING; break;
                case 'Q': board[CASTLE] |= CASTLE_WHITE_QUEEN; break;
                case 'k': board[CASTLE] |= CASTLE_BLACK_KING; break;
                case 'q': board[CASTLE] |= CASTLE_BLACK_QUEEN; break;
                case 'a': case 'b': case 'c': case 'd':
                case 'e': case 'f': case 'g': case 'h':
                        board[EP] = SQ(CHAR2FILE(*fen),WTM?RANK_5:RANK_4);
                        break;
                default:
                        break;
                }
                fen++;
        }
        reset();
        print_board();
}

/*----------------------------------------------------------------------+
 |      attack tables                                                   |
 +----------------------------------------------------------------------*/

static void atk_slide(int sq, byte dirs, struct side *s)
{

        byte dir = 0;
        int to;
	if (parallel_code){
	  fprintf(stderr, "Calling serial version of atk_slide during parallel code.\n");
	}

        dirs &= king_dirs[sq];
        do {
                dir -= dirs;
                dir &= dirs;
                to = sq;
                do {
                        to += king_step[dir];
                        s->attack[to] += 1;
                        if (board[to] != EMPTY) break;
                } while (dir & king_dirs[to]);
        } while (dirs -= dir);
}

static void compute_attacks(void)
{
        int sq, to, pc;
        byte dir, dirs;


	if (parallel_code){
	  fprintf(stderr, "Calling serial version of compute_attacks  during parallel code.\n");
	}


        memset(&white, 0, sizeof white);
        memset(&black, 0, sizeof black);

        friend = WTM ? &white : &black;
        enemy = WTM ? &black : &white;

        for (sq=0; sq<64; sq++) {
                pc = board[sq];
                if (pc == EMPTY) continue;

                switch (pc) {
                case WHITE_KING:
                        dir = 0;
                        white.king = sq;
                        dirs = king_dirs[sq];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = sq + king_step[dir];
                                white.attack[to] += 1;
                        } while (dirs -= dir);
                        break;

                case BLACK_KING:
                        dir = 0;
                        black.king = sq;
                        dirs = king_dirs[sq];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = sq + king_step[dir];
                                black.attack[to] += 1;
                        } while (dirs -= dir);
                        break;

                case WHITE_QUEEN:
                        atk_slide(sq, ATK_SLIDER, &white);
                        break;

                case BLACK_QUEEN:
                        atk_slide(sq, ATK_SLIDER, &black);
                        break;

                case WHITE_ROOK:
                        atk_slide(sq, ATK_ORTHOGONAL, &white);
                        break;

                case BLACK_ROOK:
                        atk_slide(sq, ATK_ORTHOGONAL, &black);
                        break;

                case WHITE_BISHOP:
                        atk_slide(sq, ATK_DIAGONAL, &white);
                        break;

                case BLACK_BISHOP:
                        atk_slide(sq, ATK_DIAGONAL, &black);
                        break;

                case WHITE_KNIGHT:
                        dir = 0;
                        dirs = knight_dirs[sq];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = sq + knight_jump[dir];
                                white.attack[to] += 1;
                        } while (dirs -= dir);
                        break;

                case BLACK_KNIGHT:
                        dir = 0;
                        dirs = knight_dirs[sq];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = sq + knight_jump[dir];
                                black.attack[to] += 1;
                        } while (dirs -= dir);
                        break;

                case WHITE_PAWN:
                        white.pawns[1+F(sq)] += 1;
                        if (F(sq) != FILE_H) {
                                white.attack[sq + DIR_N + DIR_E] += 1;
                        }
                        if (F(sq) != FILE_A) {
                                white.attack[sq + DIR_N - DIR_E] += 1;
                        }
                        break;

                case BLACK_PAWN:
                        black.pawns[1+F(sq)] += 1;
                        if (F(sq) != FILE_H) {
                                black.attack[sq - DIR_N + DIR_E] += 1;
                        }
                        if (F(sq) != FILE_A) {
                                black.attack[sq - DIR_N - DIR_E] += 1;
                        }
                        break;
                }
        }
}

/*----------------------------------------------------------------------+
 |      make/unmake move                                                |
 +----------------------------------------------------------------------*/

static void unmake_move(void)
{
        int sq;
	if (parallel_code){
	  fprintf(stderr, "Calling serial version of unmake_move during parallel code.\n");
	}

        for (;;) {
                sq = *--undo_sp;
                if (sq < 0) break;               /* Found sentinel */
                board[sq] = *--undo_sp;
        }
        ply--;
}

static void make_move(int move)
{
        int fr;
        int to;
        int sq;
	if (parallel_code){
	  fprintf(stderr, "Calling serial version of make_move during parallel code.\n");
	}

        *undo_sp++ = -1;                        /* Place sentinel */

        if (board[EP]) {                        /* Clear en-passant info */
                *undo_sp++ = board[EP];
                *undo_sp++ = EP;
                board[EP] = 0;
        }

        to = TO(move);
        fr = FR(move);

        if (move & SPECIAL) {                   /* Special moves first */
                switch (R(fr)) {
                case RANK_8:                    /* Black castles */
                        unmake_move();
                        if (to == G8) {
                                make_move(MOVE(H8,F8));
                        } else {
                                make_move(MOVE(A8,D8));
                        }
                        break;

                case RANK_7:
                        if (board[fr] == BLACK_PAWN) { /* Set en-passant flag */
                                *undo_sp++ = 0;
                                *undo_sp++ = EP;
                                board[EP] = to;
                        } else {                /* White promotes */
                                *undo_sp++ = board[fr];
                                *undo_sp++ = fr;
                                board[fr] = WHITE_QUEEN + (move>>13);
                        }
                        break;

                case RANK_5:                    /* White captures en-passant */
                case RANK_4:                    /* Black captures en-passant */
                        sq = SQ(F(to),R(fr));
                        *undo_sp++ = board[sq];
                        *undo_sp++ = sq;
                        board[sq] = EMPTY;
                        break;

                case RANK_2:
                        if (board[fr] == WHITE_PAWN) { /* Set en-passant flag */
                                *undo_sp++ = 0;
                                *undo_sp++ = EP;
                                board[EP] = to;
                        } else {                /* Black promotes */
                                *undo_sp++ = board[fr];
                                *undo_sp++ = fr;
                                board[fr] = BLACK_QUEEN + (move>>13);
                        }
                        break;

                case RANK_1:                    /* White castles */
                        unmake_move();
                        if (to == G1) {
                                make_move(MOVE(H1,F1));
                        } else {
                                make_move(MOVE(A1,D1));
                        }
                        break;

                default:
                        break;
                }
        }

        ply++;
        if (board[to]!=EMPTY ||
            board[fr]==WHITE_PAWN || board[fr]==BLACK_PAWN
        ) {
                *undo_sp++ = board[LAST];
                *undo_sp++ = LAST;
                board[LAST] = ply;
        }

        *undo_sp++ = board[to];
        *undo_sp++ = to;
        *undo_sp++ = board[fr];
        *undo_sp++ = fr;

        board[to] = board[fr];
        board[fr] = EMPTY;

        if (board[CASTLE] & (castle[fr] | castle[to])) {
                *undo_sp++ = board[CASTLE];
                *undo_sp++ = CASTLE;
                board[CASTLE] &= ~(castle[fr] | castle[to]);
        }
}

/*----------------------------------------------------------------------+
 |      move generator                                                  |
 +----------------------------------------------------------------------*/

/* XXX Dirty hack. Used to signal normal move generation or
   generation of good captures only */
static unsigned short caps;

static int push_move(int fr, int to)
{
        unsigned short prescore = PRESCORE_EQUAL;
        int move;

	if (parallel_code){
	  fprintf(stderr, "Calling serial version of push_move during parallel code.\n");
	}


        /* what do we capture */
        if (board[to] != EMPTY) {
                prescore += (1<<9) + prescore_piece_value[board[to]];
        }

        /* does the destination square look safe? */
        if (WTM) {
                if (black.attack[to] != 0) { /* defended */
                        prescore -= prescore_piece_value[board[fr]];
                }
        } else {
                if (white.attack[to] != 0) { /* defended */
                        prescore -= prescore_piece_value[board[fr]];
                }
        }

        if (prescore >= caps) {
                move = MOVE(fr, to);
                move_sp->move = move;

		/*SIMPLE history was removed*/
                move_sp->prescore = prescore /*| history[move]*/;
                move_sp++;
                return 1;
        }
        return 0;
}

static void push_special_move(int fr, int to)
{
        int move;
	if (parallel_code){
	  fprintf(stderr, "Calling serial version of push_special during parallel code.\n");
	}




        move = MOVE(fr, to);

	/*SIMPLE history was removed*/
        move_sp->prescore = PRESCORE_EQUAL /*| history[move]*/;
        move_sp->move = move | SPECIAL;
        move_sp++;
}

static void push_pawn_move(int fr, int to)
{
	if (parallel_code){
	  fprintf(stderr, "Calling serial version of push_pawn during parallel code.\n");
	}


        if ((R(to) == RANK_8) || (R(to) == RANK_1)) {
                push_special_move(fr, to);          /* queen promotion */
                push_special_move(fr, to);          /* rook promotion */
                move_sp[-1].move += 1<<13;
                push_special_move(fr, to);          /* bishop promotion */
                move_sp[-1].move += 2<<13;
                push_special_move(fr, to);          /* knight promotion */
                move_sp[-1].move += 3<<13;
        } else {
                push_move(fr, to);
        }
}

static void gen_slides(int fr, byte dirs)
{
        int vector;
        int to;
        byte dir = 0;
        dirs &= king_dirs[fr];


	if (parallel_code){
	  fprintf(stderr, "Calling serial version of gen_slides during parallel code.\n");
	}


        do {
                dir -= dirs;
                dir &= dirs;
                vector = king_step[dir];
                to = fr;
                do {
                        to += vector;
                        if (board[to] != EMPTY) {
                                if (PIECE_COLOR(board[to]) != WTM) {
                                        push_move(fr, to);
                                }
                                break;
                        }
                        push_move(fr, to);
                } while (dir & king_dirs[to]);
        } while (dirs -= dir);
}

static int cmp_move(const void *ap, const void *bp)
{
        const struct move *a = ap;
        const struct move *b = bp;

        if (a->prescore < b->prescore) return -1;
        if (a->prescore > b->prescore) return 1;
        return a->move - b->move; /* this makes qsort deterministic */
}

static int test_illegal(int move)
{
	if (parallel_code){
	  fprintf(stderr, "Calling serial version of test_illegal during parallel code.\n");
	}

        make_move(move);
        compute_attacks();
        unmake_move();
        return friend->attack[enemy->king] != 0;
}

static void generate_moves(unsigned treshold)
{
        int             fr, to;
        int             pc;
        byte            dir, dirs;
	if (parallel_code){
	  fprintf(stderr, "Calling serial version of generate moves during parallel code.\n");
	}

        caps = treshold;

        for (fr=0; fr<64; fr++) {
                pc = board[fr];
                if (!pc || PIECE_COLOR(pc) != WTM) continue;

                /*
                 *  generate moves for this piece
                 */
                switch (pc) {
                case WHITE_KING:
                case BLACK_KING:
                        dir = 0;
                        dirs = king_dirs[fr];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = fr+king_step[dir];
                                if (board[to] != EMPTY &&
                                    PIECE_COLOR(board[to]) == WTM) continue;
                                push_move(fr, to);
                        } while (dirs -= dir);
                        break;

                case WHITE_QUEEN:
                case BLACK_QUEEN:
                        gen_slides(fr, ATK_SLIDER);
                        break;

                case WHITE_ROOK:
                case BLACK_ROOK:
                        gen_slides(fr, ATK_ORTHOGONAL);
                        break;

                case WHITE_BISHOP:
                case BLACK_BISHOP:
                        gen_slides(fr, ATK_DIAGONAL);
                        break;

                case WHITE_KNIGHT:
                case BLACK_KNIGHT:
                        dir = 0;
                        dirs = knight_dirs[fr];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = fr+knight_jump[dir];
                                if (board[to] != EMPTY &&
                                    PIECE_COLOR(board[to]) == WTM) continue;
                                push_move(fr, to);
                        } while (dirs -= dir);
                        break;

                case WHITE_PAWN:
                        if (F(fr) != FILE_H) {
                                to = fr + DIR_N + DIR_E;
                                if (board[to] >= BLACK_KING) {
                                        push_pawn_move(fr, to);
                                }
                        }
                        if (F(fr) != FILE_A) {
                                to = fr + DIR_N - DIR_E;
                                if (board[to] >= BLACK_KING) {
                                        push_pawn_move(fr, to);
                                }
                        }
                        to = fr + DIR_N;
                        if (board[to] != EMPTY) {
                                break;
                        }
                        push_pawn_move(fr, to);
                        if (R(fr) == RANK_2) {
                                to += DIR_N;
                                if (board[to] == EMPTY) {
                                        if (push_move(fr, to))
                                        if (black.attack[to-DIR_N]) {
                                                move_sp[-1].move |= SPECIAL;
                                        }
                                }
                        }
                        break;

                case BLACK_PAWN:
                        if (F(fr) != FILE_H) {
                                to = fr - DIR_N + DIR_E;
                                if (board[to] && board[to] < BLACK_KING) {
                                        push_pawn_move(fr, to);
                                }
                        }
                        if (F(fr) != FILE_A) {
                                to = fr - DIR_N - DIR_E;
                                if (board[to] && board[to] < BLACK_KING) {
                                        push_pawn_move(fr, to);
                                }
                        }
                        to = fr - DIR_N;
                        if (board[to] != EMPTY) {
                                break;
                        }
                        push_pawn_move(fr, to);
                        if (R(fr) == RANK_7) {
                                to -= DIR_N;
                                if (board[to] == EMPTY) {
                                        if (push_move(fr, to))
                                        if (white.attack[to+DIR_N]) {
                                                move_sp[-1].move |= SPECIAL;
                                        }
                                }
                        }
                        break;
                }
        }

        /*
         *  generate castling moves
         */
        if (board[CASTLE] && !enemy->attack[friend->king]) {
                if (WTM && (board[CASTLE] & CASTLE_WHITE_KING) &&
                        !board[F1] && !board[G1] &&
                        !enemy->attack[F1])
                {
                        push_special_move(E1, G1);
                }
                if (WTM && (board[CASTLE] & CASTLE_WHITE_QUEEN) &&
                        !board[D1] && !board[C1] && !board[B1] &&
                        !enemy->attack[D1])
                {
                        push_special_move(E1, C1);
                }
                if (!WTM && (board[CASTLE] & CASTLE_BLACK_KING) &&
                        !board[F8] && !board[G8] &&
                        !enemy->attack[F8])
                {
                        push_special_move(E8, G8);
                }
                if (!WTM && (board[CASTLE] & CASTLE_BLACK_QUEEN) &&
                        !board[D8] && !board[C8] && !board[B8] &&
                        !enemy->attack[D8])
                {
                        push_special_move(E8, C8);
                }
        }

        /*
         *  generate en-passant captures
         */
        if (board[EP]) {
                int ep = board[EP];

                if (WTM) {
                        if (F(ep) != FILE_A && board[ep-DIR_E] == WHITE_PAWN) {
                                if (push_move(ep-DIR_E, ep+DIR_N))
                                        move_sp[-1].move |= SPECIAL;
                        }
                        if (F(ep) != FILE_H && board[ep+DIR_E] == WHITE_PAWN) {
                                if (push_move(ep+DIR_E, ep+DIR_N))
                                        move_sp[-1].move |= SPECIAL;
                        }
                } else {
                        if (F(ep) != FILE_A && board[ep-DIR_E] == BLACK_PAWN) {
                                if (push_move(ep-DIR_E, ep-DIR_N))
                                        move_sp[-1].move |= SPECIAL;
                        }
                        if (F(ep) != FILE_H && board[ep+DIR_E] == BLACK_PAWN) {
                                if (push_move(ep+DIR_E, ep-DIR_N))
                                        move_sp[-1].move |= SPECIAL;
                        }
                }
        }
}

static void print_move_san(int move)
{
        int fr, to;
        int filex = 0, rankx = 0;
        struct move *moves;

        fr = FR(move);
        to = TO(move);

        if ((move==(MOVE(E1,C1)|SPECIAL)) || (move==(MOVE(E8,C8)|SPECIAL))) {
                fputs("O-O-O", stdout);
                goto check;
        }
        if ((move==(MOVE(E1,G1)|SPECIAL)) || (move==(MOVE(E8,G8)|SPECIAL))) {
                fputs("O-O", stdout);
                goto check;
        }

        if ((board[fr]==WHITE_PAWN) || (board[fr] == BLACK_PAWN)) {
                /*
                 *  pawn moves are a bit special
                 */
                if (F(fr) != F(to)) {
                        printf("%cx", FILE2CHAR(F(fr)));
                }
                print_square(to);
                /*
                 *  promote to piece (=Q, =R, =B, =N)
                 */
                if (move > 4095
                && (R(to)==RANK_1 || R(to)==RANK_8)) {
                        putchar('=');
                        putchar("QRBN"[move>>13]);
                }
                goto check;
        }

        /*
         *  piece identifier (K, Q, R, B, N)
         */
        putchar(toupper(PIECE2CHAR(board[fr])));

        /*
         *  disambiguate: consider moves of identical pieces to the same square
         */
        moves = move_sp;
        generate_moves(0);
        while (move_sp > moves) {
                move_sp--;
                if (to != TO(move_sp->move)             /* same destination */
                ||  move == move_sp->move               /* different move */
                ||  board[fr] != board[FR(move_sp->move)] /* same piece */
                ||  test_illegal(move_sp->move)) {
                        continue;
                }
                rankx |= (R(fr) == R(FR(move_sp->move))) + 1;
                filex |=  F(fr) == F(FR(move_sp->move));
        }
        if (rankx != filex) putchar(FILE2CHAR(F(fr)));
        if (filex) putchar(RANK2CHAR(R(fr)));

        /*
         *  capture sign
         */
        if (board[to] != EMPTY) putchar('x');

        /*
         *  to-square
         */
        print_square(to);

        /*
         *  check ('+') or checkmate ('#')
         */
check:
        make_move(move);
        compute_attacks();
        if (enemy->attack[friend->king]) {      /* in check, is mate? */
                int sign = '#';
                moves = move_sp;
                generate_moves(0);
                while (move_sp > moves) {
                        move_sp--;
                        if (!test_illegal(move_sp->move)) {
                                sign = '+';
                                move_sp = moves;        /* break */
                        }
                }
                putchar(sign);
        }
        unmake_move();
}

/*----------------------------------------------------------------------+
 |      move parser                                                     |
 +----------------------------------------------------------------------*/

static int parse_move(char *line, int *num)
{
        int                     move, matches;
        int                     n = 0;
        struct move             *m;
        char                    *piece = NULL;
        char                    *fr_file = NULL;
        char                    *fr_rank = NULL;
        char                    *to_file = NULL;
        char                    *to_rank = NULL;
        char                    *prom_piece = NULL;
        char                    *s;

        while (xisspace(line[n]))      /* skip white space */
                n++;

        if (!strncmp(line+n, "o-o-o", 5)
        ||  !strncmp(line+n, "O-O-O", 5)
        ||  !strncmp(line+n, "0-0-0", 5)) {
                piece = "K"; fr_file = "e"; to_file = "c";
                n+=5;
        }
        else if (!strncmp(line+n, "o-o", 3)
        ||  !strncmp(line+n, "O-O", 3)
        ||  !strncmp(line+n, "0-0", 3)) {
                piece = "K"; fr_file = "e"; to_file = "g";
                n+=3;
        }
        else {
                s = strchr("KQRBNP", line[n]);
                if (s && *s) {
                        piece = s;
                        n++;
                }

                /* first square */

                s = strchr("abcdefgh", line[n]);
                if (s && *s) {
                        to_file = s;
                        n++;
                }

                s = strchr("12345678", line[n]);
                if (s && *s) {
                        to_rank = s;
                        n++;
                }

                if (line[n] == '-' || line[n] == 'x') {
                        n++;
                }

                s = strchr("abcdefgh", line[n]);
                if (s && *s) {
                        fr_file = to_file;
                        fr_rank = to_rank;
                        to_file = s;
                        to_rank = NULL;
                        n++;
                }

                s = strchr("12345678", line[n]);
                if (s && *s) {
                        to_rank = s;
                        n++;
                }

                if (line[n] == '=') {
                        n++;
                }
                s = strchr("QRBNqrbn", line[n]);
                if (s && *s) {
                        prom_piece = s;
                        n++;
                }
        }

        while (line[n] == '+' || line[n] == '#'
        || line[n] == '!' || line[n] == '?') {
                n++;
        }

        *num = n;
        if (!piece && !fr_file && !fr_rank
        && !to_file && !to_rank && !prom_piece)
                return 0;

        move = 0;
        matches = 0;

        m = move_sp;
        compute_attacks();
        generate_moves(0);
        while (move_sp > m) {
                int fr, to;

                move_sp--;

                fr = FR(move_sp->move);
                to = TO(move_sp->move);

                /* does this move match? */

                if ((piece && *piece != toupper(PIECE2CHAR(board[fr])))
                 || (to_file && *to_file != FILE2CHAR(F(to)))
                 || (to_rank && *to_rank != RANK2CHAR(R(to)))
                 || (fr_file && *fr_file != FILE2CHAR(F(fr)))
                 || (fr_rank && *fr_rank != RANK2CHAR(R(fr)))
                 || (prom_piece &&
                        (toupper(*prom_piece) != "QRBN"[(move_sp->move)>>13])))
                {
                        continue;
                }

                /* if so, is it legal? */
                if (test_illegal(move_sp->move)) {
                        continue;
                }
                /* probably.. */

                /* do we already have a match? */
                if (move) {
                        int old_is_p, new_is_p;

                        if (piece) {
                                puts("ambiguous!"); print_board(); puts(line);
                                move_sp = m;
                                return 0;
                        }
                        /* if this move is a pawn move and the previously
                           found isn't, we accept it */
                        old_is_p = (board[FR(move)]==WHITE_PAWN) ||
                                (board[FR(move)]==BLACK_PAWN);
                        new_is_p = (board[fr]==WHITE_PAWN) ||
                                (board[fr]==BLACK_PAWN);

                        if (new_is_p && !old_is_p) {
                                move = move_sp->move;
                                matches = 1;
                        } else if (old_is_p && !new_is_p) {
                        } else if (!old_is_p && !new_is_p)  {
                                matches++;
                        } else if (old_is_p && new_is_p)  {
                                puts("ambiguous!"); print_board(); puts(line);
                                move_sp = m;
                                return 0;
                        }
                } else {
                        move = move_sp->move;
                        matches = 1;
                }
        }
        if (matches <= 1)
                return move;

        puts("ambiguous!"); print_board(); puts(line);
        return 0;
}

/*----------------------------------------------------------------------+
 |      opening book                                                    |
 +----------------------------------------------------------------------*/

static int cmp_bk(const void *ap, const void *bp)
{
        const struct bk *a = ap;
        const struct bk *b = bp;

        if (a->hash < b->hash) return -1;
        if (a->hash > b->hash) return 1;
        return (int)a->move - (int)b->move;
}

static void compact_book(void)
{
  
  long b = 0, c = 0;

        qsort(BOOK, booksize, sizeof(BOOK[0]), cmp_bk);
        while (b<booksize) {
                BOOK[c] = BOOK[b];
                b++;
                while (b<booksize && !cmp_bk(&BOOK[c], &BOOK[b])) {
                        BOOK[c].count += BOOK[b].count;
                        b++;
                }
                c++;
        }
        booksize = c;
}

static void load_book(char *filename)
{
        FILE                    *fp;
        char                    line[128], *s;
        int                     num, move;
	
        booksize = 0;

        fp = fopen(filename, "r");
        if (!fp) {
                printf("no opening book: %s\n", filename);
                exit(EXIT_FAILURE);     /* no mercy */
        }
        while (readline(line, sizeof(line), fp) >= 0) {
                s = line;
                for (;;) {
                        move = parse_move(s, &num);
                        if (!move) break;

                        s += num;
                        if (booksize < CORE) {
                                BOOK[booksize].hash = compute_hash();
                                BOOK[booksize].move = move;
                                BOOK[booksize].count = 1;
                                booksize++;
                                if (booksize >= CORE) compact_book();
                        }
                        make_move(move);
                }
                while (ply>0) unmake_move(); /* @@@ wrong */
        }
        fclose(fp);
        compact_book();
}

static int book_move(void)
{

        int move = 0, sum = 0;
        long x = 0, y, m;
        char *seperator = "book:";
        unsigned long hash;
  printf("Book move.\n");
        if (!booksize) return 0;
        y = booksize;
        hash = compute_hash();
        while (y-x > 1) { /* inv: BOOK[x].hash <= hash < BOOK[y].hash */
                m = (x + y) / 2;
                if (hash < BOOK[m].hash) { y=m; } else { x=m; }
        }
        while (BOOK[x].hash == hash) {
                printf("%s (%d)", seperator, BOOK[x].count);
                seperator = ",";
                print_move_san(BOOK[x].move);
                compute_hash();

                sum += BOOK[x].count;
                if (rnd()%sum < BOOK[x].count) {
                        move = BOOK[x].move;
                }
                if (!x--) break;
        }
        putchar('\n');
        return move;
}

/*----------------------------------------------------------------------+
 |      evaluator                                                       |
 +----------------------------------------------------------------------*/

static int center[64] = {
         0,  2,  3,  4,  4,  3,  2,  0,
         2,  3,  4,  5,  5,  4,  3,  2,
         3,  4,  5,  7,  7,  5,  4,  3,
         4,  5,  7, 10, 10,  7,  5,  4,
         4,  5,  7, 10, 10,  7,  5,  4,
         3,  4,  5,  7,  7,  5,  4,  3,
         2,  3,  4,  5,  5,  4,  3,  2,
         0,  2,  3,  4,  4,  3,  2,  0,
};

static int pawn_advance[64] = {
        0,  0, 2, 3, 5, 30, 50, 0,
        0,  0, 2, 3, 5, 30, 50, 0,
        0,  0, 2, 3, 5, 30, 50, 0,
        0, -5, 2, 8, 8, 30, 50, 0,
        0, -5, 2, 8, 8, 30, 50, 0,
        0,  0, 2, 3, 5, 30, 50, 0,
        0,  0, 2, 3, 5, 30, 50, 0,
        0,  0, 2, 3, 5, 30, 50, 0,
};

#define DIFF(a,b) ((a)>(b) ? (a)-(b) : (b)-(a))
#define TAXI(a,b) (DIFF(F(a),F(b)) + DIFF(R(a),R(b)))

static void compute_piece_square_tables(void)
{
        int pc, sq;
        int score = 0;
        int file;

        compute_attacks(); /* so we know where the king and pawns are */

        for (sq=0; sq<64; sq++) {
                file = 1+F(sq);
                for (pc=1; pc<13; pc++) {
                        switch (pc) {
                        case WHITE_KNIGHT:
                                score = 300;
                                score += center[sq];
                                score -= TAXI(black.king, sq);
                                break;

                        case BLACK_KNIGHT:
                                score = -300;
                                score -= center[sq];
                                score += TAXI(white.king, sq);
                                break;

                        case WHITE_BISHOP:
                                score = +300;
                                break;

                        case BLACK_BISHOP:
                                score = -300;
                                break;

                        case WHITE_ROOK:
                                score = +500;
                                score += DIFF(F(black.king), F(sq));
                                break;

                        case BLACK_ROOK:
                                score = -500;
                                score -= DIFF(F(white.king), F(sq));
                                break;

                        case WHITE_QUEEN:
                                score = +900;
                                score += center[sq];
                                break;

                        case BLACK_QUEEN:
                                score = -900;
                                score -= center[sq];
                                break;

                        case WHITE_KING:
                                score = +400;
                                score += center[sq];
                                break;

                        case BLACK_KING:
                                score = -400;
                                score -= center[sq];
                                break;

                        case WHITE_PAWN:
                                score = +100;
                                score += pawn_advance[sq];
                                if (!black.pawns[file]) {
                                        score += pawn_advance[sq];
                                }
                                break;

                        case BLACK_PAWN:
                                score = -100;
                                score -= pawn_advance[FLIP(sq)];
                                if (!white.pawns[file]) {
                                        score -= pawn_advance[FLIP(sq)];
                                }
                                break;
                        }
                        piece_square[pc-1][sq] = score;
                }
        }
        piece_square[WHITE_KING-1][G1] += 100;
        piece_square[BLACK_KING-1][G8] -= 100;
}

static int white_control[64] = {
          1,  1,  1,  1,  2,  2,  2,  2,
          1,  1,  1,  1,  2,  2,  2,  2,
          1,  1,  2,  2,  3,  3,  2,  2,
          1,  1,  2,  5,  6,  3,  2,  2,
          1,  1,  2,  5,  6,  3,  2,  2,
          1,  1,  2,  2,  3,  3,  2,  2,
          1,  1,  1,  1,  2,  2,  2,  2,
          1,  1,  1,  1,  2,  2,  2,  2,
};

static int evaluate(void)
{
        int sq;
        int score = 0;

        int white_has, black_has;

if (parallel_code){
	  fprintf(stderr, "Calling serial version of evailuate during parallel code.\n");
	}

        /*
         *  stage 1: material+piece_square tables
         */
        for (sq=0; sq<64; sq++) {
                int file;

                if (board[sq] == EMPTY) continue;
                score += piece_square[board[sq]-1][sq];

                file = F(sq)+1;
                switch (board[sq]) {
                case WHITE_PAWN:
                {
                        int missing;
                        if (white.pawns[file] > 1) {
                                score -= 15;
                        }
                        missing = !white.pawns[file-1] + !white.pawns[file+1] +
                                !black.pawns[file];
                        score -= missing * missing * 5;
                        break;
                }
                case BLACK_PAWN:
                {
                        int missing;
                        if (black.pawns[file] > 1) {
                                score += 15;
                        }
                        missing = !black.pawns[file-1] + !black.pawns[file+1] +
                                !white.pawns[file];
                        score += missing * missing * 5;
                        break;
                }
                case WHITE_ROOK:
                        if (!white.pawns[file]) {
                                score += 10;
                                if (!black.pawns[file]) {
                                        score += 10;
                                }
                        }
                        break;

                case BLACK_ROOK:
                        if (!black.pawns[file]) {
                                score -= 10;
                                if (!white.pawns[file]) {
                                        score -= 10;
                                }
                        }
                        break;

                default:
                        break;
                }
        }

        /*
         *  stage 2: board control
         */
        white_has = 0;
        black_has = 0;
        for (sq=0; sq<64; sq++) {
                if (white.attack[sq] > black.attack[sq]) {
                        white_has += white_control[sq];
                }
                if (white.attack[sq] < black.attack[sq]) {
                        black_has += white_control[FLIP(sq)];
                }
        }
        score += (white_has - black_has);



        /* some noise to randomize play */
	/*But only for the first few turns of the game.*/
	

	if (random_countdown>0){
	  //score += (hash_stack[ply] ^ rnd_seed) % 17 - 8;
	  score += (rnd()) % 91 - 46;
	  
	}


        return WTM ? score : -score;
}

/*----------------------------------------------------------------------+
 |      search                                                          |
 +----------------------------------------------------------------------*/

static int qsearch(int alpha, int beta)
{
        int                             best_score;
        int                             score;
        struct move                     *moves;

	nodes++;
        

	

if (parallel_code){
	  fprintf(stderr, "Calling serial version of qsearch during parallel code.\n");
	}

	/*SIMPLE no hash
	  hash_stack[ply] = compute_hash();*/
        best_score = evaluate();
        if (best_score >= beta) {
                return best_score;
        }

        moves = move_sp;
        generate_moves(PRESCORE_EQUAL + (1<<9));
        qsort(moves, move_sp - moves, sizeof(*moves), cmp_move);
        while (move_sp > moves) {
                int move;
		//nodes++;
                move_sp--;
                move = move_sp->move;
                make_move(move);

                compute_attacks();
                if (friend->attack[enemy->king]) {
                        unmake_move();
                        continue;
                }

                score = - qsearch(-beta, -alpha);

                unmake_move();

                if (score <= best_score) {
                        continue;
                }
                best_score = score;

                if (score <= alpha) {
                        continue;
                }
                alpha = score;

                if (score < beta) {
                        continue;
                }
				move_sp = moves; /* fail high: skip remaining moves */
        }
        return best_score;
}

static int search(int depth, int alpha, int beta)
{
        int                             best_score = -INF;
        int                             best_move = 0;
        int                             score;
        struct move                     *moves;
        int                             incheck = 0;

if (parallel_code){
	  fprintf(stderr, "Calling serial version of search during parallel code.\n");
	}


        /*SIMPLE we cut these variables when we don't use the hash table
       
	struct tt                       *tt;
        int                             oldalpha = alpha;
	int                             oldbeta = beta;
        int                             i, count=0;
	*/

//nodes++;



	/*SIMPLE no hash stack
	  test for draw by repetition*/ 
        /*hash_stack[ply] = compute_hash();
        for (i=ply-4; i>=board[LAST]; i-=2) {
                if (hash_stack[i] == hash_stack[ply]) count++;
                if (count>=2) return 0;
		}*/

        
	/*  check transposition table*/
	/*
        tt = &TTABLE[ ((hash_stack[ply]>>16) & (CORE-1)) ];
        if (tt->hash == (hash_stack[ply] & 0xffffU)) {
                if (tt->depth >= depth) {
                        if (tt->flag >= 0) alpha = MAX(alpha, tt->score);
                        if (tt->flag <= 0) beta = MIN(beta,  tt->score);
                        if (alpha >= beta) return tt->score;
                }
                best_move = tt->move & 07777;
        }

        history[best_move] |= PRESCORE_HASHMOVE;*/
        incheck = enemy->attack[friend->king];

        /*
         *  generate moves
         */
        moves = move_sp;
        generate_moves(0);

        history[best_move] &= 0x3fff;
        best_move = 0;

        qsort(moves, move_sp - moves, sizeof(*moves), cmp_move);

        /*
         *  loop over all moves
         */
        while (move_sp > moves) {
	  //nodes++;
                int newdepth;
                int move;
                move_sp--;
                move = move_sp->move;
                make_move(move);
                compute_attacks();
                if (friend->attack[enemy->king]) {
                        unmake_move();
                        continue;
                }

                newdepth = incheck ? depth : depth-1;
                if (newdepth <= 0) {
                        score = -qsearch(-beta, -alpha);
                } else {
                        score = -search(newdepth, -beta, -alpha);
                }
                if (score < -29000) score++;    /* adjust for mate-in-n */

                unmake_move();

                if (score <= best_score) continue;
                best_score = score;
                best_move = move;

                if (score <= alpha) continue;
                alpha = score;

                if (score < beta) continue;

		                move_sp = moves; /* fail high: skip remaining moves */
        }

        if (best_score == -INF) { /* deal with mate and stalemate */
                if (incheck) {
                        best_score = -30000;
                } else {
                        best_score = 0;
                }
        }




	/*	SIMPLE getting rid of hash table
       history[best_move & 07777] += depth*depth;
        if (history[best_move & 07777] > 511) {
                int m;
                for (m=0; m<SPECIAL; m++) {
		history[m] >>= 4;  */   /*  scaling */
	/*}
        }

        tt->hash = hash_stack[ply] & 0xffffU;
        tt->move = best_move;
        tt->score = best_score;
        tt->depth = depth;
        tt->flag = (oldalpha < best_score) - (best_score < oldbeta);
	*/
        return best_score;
}

/* squeeze: compacts a long into a short */
static unsigned short squeeze(unsigned long n)
{
        const unsigned long mask = ~0U<<11;
        unsigned short s;

        for (s=0; n&mask; n>>=1, s-=mask)
                /* SKIP */;

        return s | n;
}

static int root_search(int maxdepth, FILE* write_time,FILE*write_first_time, FILE*write_second_time, FILE*write_count, FILE*write_first_count, FILE*write_second_count)
{
        int             depth;
        int             score, best_score;
        int             move = 0;
        int             alpha, beta;
        unsigned long   node;
        struct move     *m;
	int do_mid=1;
	double initialize,top, mid, bottom;
	int top_nodes, mid_nodes, bottom_nodes;

        nodes = 0;
        compute_piece_square_tables();

        generate_moves(0);
        qsort(move_stack, move_sp-move_stack, sizeof(struct move), cmp_move);

        alpha = -INF;
        beta = +INF;
        puts(" nodes ply score move");

        for (depth = 1; depth <= maxdepth; depth++) {
	  do_mid=1;
	  top=currentSeconds();
	  top_nodes=nodes;
                m = move_stack;
                best_score = INT_MIN;

                node = nodes;
                while (m < move_sp) {

		  /*go into move, check if legal;*/
                        make_move(m->move);
                        compute_attacks();
                        if (friend->attack[enemy->king] != 0) { /* illegal? */
                                unmake_move();
                                *m = *--move_sp; /* drop this move */
                                continue;
                        }


			/*			 SIMPLE No hash stack
			//don't know what this is anyway
			hash_stack[ply] = compute_hash();*/
			

			/*do normal search. Or if end of depth, Q-Search*/
                        if (depth-1 > 0) {
                                score = -search(depth-1, -beta, -alpha);
                        } else {
                                score = -qsearch(-beta, -alpha);
                        }
                        unmake_move();


			/*Fix window if it was too narrow.*/
                        if (score>=beta || (score<=alpha && m==move_stack)) {
                                alpha = -INF;
                                beta = +INF;
                                continue; /* re-search this move */
                        }
			
			/*I don't know what this is*/
                        m->prescore = ~squeeze(nodes-node);
                        node = nodes;

			/*		if(ply==147 && depth==1){
			printf("A DEBUG %5lu %3d %+1.2f ", nodes, depth, score / 100.0);
			print_move_san(m->move);
			puts("");
			}*/


                        if ((score > best_score) || ((score==best_score)&&(m->move>move))) {
			//                        if (score > best_score) {
                                struct move tmp;

                                best_score = score;
                                alpha = score-1;
                                beta = score + 2;
                                move = m->move;

                                tmp = *move_stack; /* swap with top of list */
                                *move_stack = *m;
                                *m = tmp;
                        }
                        m++; /* continue with next move */
			if((do_mid>0)&&(depth>=maxdepth-1)){
			  mid=currentSeconds();
			  mid_nodes=nodes;
			  fprintf(stderr, "Depth %d: First half time %f nodes is %d\n", depth, mid-top, mid_nodes-top_nodes);
			  fprintf(write_first_time, "%f\n", mid-top);
			  fprintf(write_first_count, "%d\n", mid_nodes-top_nodes);
			  do_mid--;
			}
  


 
                }

		//SIMPLE This code doesn't work in the parallel section.
		/*if (move_sp-move_stack <= 1) {
                        break; //just one move to play 
			}*/
	total_nodes_visited+=nodes;
	fprintf(stderr, "Serial nodes visited was %d, total so far is %d\n", nodes, total_nodes_visited);
                printf(" %3d %+1.2f ", depth, best_score / 100.0);
                print_move_san(move);
                puts("");

                /* sort remaining moves in descending order of subtree size */
                //SIMPLE I think this relies on the hash table anyway.
		//qsort(move_stack+1, move_sp-move_stack-1, sizeof(*m), cmp_move);

		/*Widen window for deeper search*/
                alpha = best_score - 33;        /* aspiration window */
                beta = best_score + 33;

		if(depth>=maxdepth-1){
		  bottom=currentSeconds();
		  bottom_nodes=nodes;
		  fprintf(stderr, "Depth %d: second half time %f and nodes %d\n", depth, bottom-mid, bottom_nodes-mid_nodes);
		  fprintf(write_second_time, "%f\n", bottom-mid);
		  fprintf(write_second_count, "%d\n", bottom_nodes-mid_nodes);
		}
		
        }
        move_sp = move_stack;
	fprintf(write_count, "%d\n", nodes);
        return move;
}

static int root_time(int maxdepth, FILE* write_time,FILE*write_first_time, FILE*write_second_time, FILE*write_count, FILE*write_first_count, FILE*write_second_count)
{
        int             depth;
        int             score, best_score;
        int             move = 0, return_move=0;
        int             alpha, beta;
        unsigned long   node;
        struct move     *m;
	int do_mid=1;
	double initialize,top, mid, bottom;
	int top_nodes, mid_nodes, bottom_nodes;
	initialize=currentSeconds();
        nodes = 0;
        compute_piece_square_tables();

        generate_moves(0);
        qsort(move_stack, move_sp-move_stack, sizeof(struct move), cmp_move);

        alpha = -INF;
        beta = +INF;
        puts(" nodes ply score move");

        for (depth = 1; (currentSeconds()-initialize<TIME_LIMIT)|| (depth<=1); depth++) {
	  global_depth=depth-1;
	  return_move=move;
	  do_mid=1;
	  top=currentSeconds();
	  top_nodes=nodes;
                m = move_stack;
                best_score = INT_MIN;

                node = nodes;
                while (m < move_sp) {

		  /*go into move, check if legal;*/
                        make_move(m->move);
                        compute_attacks();
                        if (friend->attack[enemy->king] != 0) { /* illegal? */
                                unmake_move();
                                *m = *--move_sp; /* drop this move */
                                continue;
                        }


			/*			 SIMPLE No hash stack
			//don't know what this is anyway
			hash_stack[ply] = compute_hash();*/
			

			/*do normal search. Or if end of depth, Q-Search*/
                        if (depth-1 > 0) {
                                score = -search(depth-1, -beta, -alpha);
                        } else {
                                score = -qsearch(-beta, -alpha);
                        }
                        unmake_move();


			/*Fix window if it was too narrow.*/
                        if (score>=beta || (score<=alpha && m==move_stack)) {
                                alpha = -INF;
                                beta = +INF;
                                continue; /* re-search this move */
                        }
			
			/*I don't know what this is*/
                        m->prescore = ~squeeze(nodes-node);
                        node = nodes;

			/*		if(ply==147 && depth==1){
			printf("A DEBUG %5lu %3d %+1.2f ", nodes, depth, score / 100.0);
			print_move_san(m->move);
			puts("");
			}*/


                        if ((score > best_score) || ((score==best_score)&&(m->move>move))) {
			//                        if (score > best_score) {
                                struct move tmp;

                                best_score = score;
                                alpha = score-1;
                                beta = score + 2;
                                move = m->move;

                                tmp = *move_stack; /* swap with top of list */
                                *move_stack = *m;
                                *m = tmp;
                        }
                        m++; /* continue with next move */
			if((do_mid>0)&&(depth>=maxdepth-1)){
			  mid=currentSeconds();
			  mid_nodes=nodes;
			  /*fprintf(stderr, "Depth %d: First half time %f nodes is %d\n", depth, mid-top, mid_nodes-top_nodes);
			  fprintf(write_first_time, "%f\n", mid-top);
			  fprintf(write_first_count, "%d\n", mid_nodes-top_nodes);*/
			  do_mid--;
			}
  


 
                }

		//SIMPLE This code doesn't work in the parallel section.
		/*if (move_sp-move_stack <= 1) {
                        break; //just one move to play 
			}*/
	total_nodes_visited+=nodes;
	/*fprintf(stderr, "Serial nodes visited was %d, total so far is %d\n", nodes, total_nodes_visited);
                printf(" %3d %+1.2f ", depth, best_score / 100.0);
                print_move_san(move);
                puts("");*/

                /* sort remaining moves in descending order of subtree size */
                //SIMPLE I think this relies on the hash table anyway.
		//qsort(move_stack+1, move_sp-move_stack-1, sizeof(*m), cmp_move);

		/*Widen window for deeper search*/
                alpha = best_score - 33;        /* aspiration window */
                beta = best_score + 33;

		if(depth>=maxdepth-1){
		  bottom=currentSeconds();
		  bottom_nodes=nodes;
		  /*fprintf(stderr, "Depth %d: second half time %f and nodes %d\n", depth, bottom-mid, bottom_nodes-mid_nodes);
		  fprintf(write_second_time, "%f\n", bottom-mid);
		  fprintf(write_second_count, "%d\n", bottom_nodes-mid_nodes);*/
		}
		
        }
        move_sp = move_stack;
	fprintf(write_count, "%d\n", nodes);
        return return_move;
}

/*----------------------------------------------------------------------+
 |      commands                                                        |
 +----------------------------------------------------------------------*/

static void cmd_bd(char *dummy /* @unused */)
{
        print_board();
}

static void cmd_book(char *dummy)
{
        int move;

        printf("%ld moves in book\n", booksize);

        move = book_move();
        if (move) {
                print_move_san(move);
                puts(" selected");
        }
}

static void cmd_list_moves(char *dummy)
{
        struct move *m;
        int nmoves = 0;

        puts("moves are:");

        compute_attacks();
        m = move_sp;
        generate_moves(0);
        qsort(m, move_sp - m, sizeof(*m), cmp_move);

        while (move_sp > m) {
                --move_sp;
                if (test_illegal(move_sp->move)) continue;
                print_move_san(move_sp->move);
                putchar('\n');
                nmoves++;
        }
        printf("%d move%s\n", nmoves, nmoves==1 ? "" : "s");
}

static void cmd_default(char *s)
{
        int move, dummy;

        move = parse_move(s, &dummy);
        if (move) {
                make_move(move);
                hash_stack[ply] = compute_hash();
                print_board();
        } else {
                printf("no such move or command: %s\n", s);
        }
}

static void cmd_undo(char *dummy)
{
        if (undo_sp > undo_stack) {
                unmake_move();
                computer[0] = 0;
                computer[1] = 0;
        } else {
                puts("cannot undo move");
        }
}

static void cmd_both(char *dummy)
{
        computer[0] = 1;
        computer[1] = 1;
}

static void cmd_white(char *dummy)
{
        computer[0] = 0;
        computer[1] = 1;
        ply = 0;
        reset();
}

static void cmd_black(char *dummy)
{
        computer[0] = 1;
        computer[1] = 0;
        ply = 1;
        reset();
}

static void cmd_force(char *dummy)
{
        computer[0] = 0;
        computer[1] = 0;
}

static void cmd_go(char *dummy)
{
        computer[!WTM] = 1;
        computer[WTM] = 0;
}

static void cmd_test(char *s)
{
        int d = maxdepth;
        sscanf(s, "%*s%d", &d);
        root_search(d, NULL, NULL, NULL, NULL, NULL, NULL);

}

static void cmd_set_depth(char *s)
{
        if (1==sscanf(s, "%*s%d", &maxdepth)) {
                maxdepth = MAX(1, MIN(maxdepth, 8));
        }
        printf("maximum search depth is %d plies\n", maxdepth);
}

static void cmd_new(char *dummy)
{
  random_countdown=RANDOM_COUNTDOWN_START;
  setup_board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");
  //load_book("book.txt");
        computer[0] = 0;
        computer[1] = 1;
        
	/*SIMPLE I changed the random seed elsewhere
	  rnd_seed = time(NULL);*/
}

static void cmd_xboard(char *dummy)
{
        xboard_mode = 1;
}

static void cmd_fen(char *s)
{
        while (xisalpha(*s)) s++;
        setup_board(s);
}

static void cmd_quit(char *dummy)
{
        exit(0);
}

struct command mscp_commands[]; /* forward declaration */

static void cmd_help(char *dummy)
{
        struct command *c;

        puts("commands are:");
        c = mscp_commands;
        do {
                printf("%-8s - %s\n", c->name ? c->name : "", c->help);
        } while (c++->name != NULL);
}

struct command mscp_commands[] = {
 { "help",      cmd_help,       "show this list of commands"            },
 { "bd",        cmd_bd,         "display board"                         },
 { "ls",        cmd_list_moves, "list moves"                            },
 { "new",       cmd_new,        "new game"                              },
 { "go",        cmd_go,         "computer starts playing"               },
 { "test",      cmd_test,       "search (depth)"                        },
 { "quit",      cmd_quit,       "leave chess program"                   },
 { "sd",        cmd_set_depth,  "set maximum search depth (plies)"      },
 { "both",      cmd_both,       "computer plays both sides"             },
 { "force",     cmd_force,      "computer plays neither side"           },
 { "white",     cmd_white,      "set computer to play white"            },
 { "black",     cmd_black,      "set computer to play black"            },
 { "book",      cmd_book,       "lookup current position in book"       },
 { "undo",      cmd_undo,       "undo move"                             },
 { "quit",      cmd_quit,       "leave chess program"                   },
 { "xboard",    cmd_xboard,     "switch to xboard mode"                 },
 { "fen",       cmd_fen,        "setup new position"                    },
 { NULL,        cmd_default,    "enter moves in algebraic notation"     },
};


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Here begins akavka's parallel functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

static void p_atk_slide(int sq, byte dirs, struct side *s, byte* p_board, ball*arg_ball)
{
        byte dir = 0;
        int to;
        dirs &= king_dirs[sq];
        do {
                dir -= dirs;
                dir &= dirs;
                to = sq;
                do {
                        to += king_step[dir];
                        s->attack[to] += 1;
                        if (p_board[to] != EMPTY) break;
                } while (dir & king_dirs[to]);
        } while (dirs -= dir);
}


static void p_compute_attacks(byte* p_board, ball*arg_ball)
{
        int sq, to, pc;
        byte dir, dirs;

        memset(&(arg_ball->white), 0, sizeof arg_ball->white);
        memset(&(arg_ball->black), 0, sizeof (arg_ball->black));



        arg_ball->friend = PWTM(arg_ball->ply) ? &(arg_ball->white) : &(arg_ball->black);
        arg_ball->enemy = PWTM(arg_ball->ply) ? &(arg_ball->black) : &(arg_ball->white);



        for (sq=0; sq<64; sq++) {
                pc = p_board[sq];
                if (pc == EMPTY) continue;

                switch (pc) {
                case WHITE_KING:
                        dir = 0;
                        arg_ball->white.king = sq;
                        dirs = king_dirs[sq];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = sq + king_step[dir];
                                arg_ball->white.attack[to] += 1;
                        } while (dirs -= dir);
                        break;

                case BLACK_KING:
                        dir = 0;
                        (arg_ball->black).king = sq;
                        dirs = king_dirs[sq];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = sq + king_step[dir];
                                (arg_ball->black).attack[to] += 1;
                        } while (dirs -= dir);
                        break;

                case WHITE_QUEEN:
		  p_atk_slide(sq, ATK_SLIDER, &arg_ball->white, p_board, arg_ball);
                        break;

                case BLACK_QUEEN:
		  p_atk_slide(sq, ATK_SLIDER, &(arg_ball->black), p_board, arg_ball);
                        break;

                case WHITE_ROOK:
		  p_atk_slide(sq, ATK_ORTHOGONAL, &arg_ball->white, p_board, arg_ball);
                        break;

                case BLACK_ROOK:
		  p_atk_slide(sq, ATK_ORTHOGONAL, &(arg_ball->black), p_board, arg_ball);
                        break;

                case WHITE_BISHOP:
		  p_atk_slide(sq, ATK_DIAGONAL, &arg_ball->white, p_board, arg_ball);
                        break;

                case BLACK_BISHOP:
		  p_atk_slide(sq, ATK_DIAGONAL, &(arg_ball->black), p_board, arg_ball);
                        break;

                case WHITE_KNIGHT:
                        dir = 0;
                        dirs = knight_dirs[sq];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = sq + knight_jump[dir];
                                arg_ball->white.attack[to] += 1;
                        } while (dirs -= dir);
                        break;

                case BLACK_KNIGHT:
                        dir = 0;
                        dirs = knight_dirs[sq];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = sq + knight_jump[dir];
                                (arg_ball->black).attack[to] += 1;
                        } while (dirs -= dir);
                        break;

                case WHITE_PAWN:
                        arg_ball->white.pawns[1+F(sq)] += 1;
                        if (F(sq) != FILE_H) {
                                arg_ball->white.attack[sq + DIR_N + DIR_E] += 1;
                        }
                        if (F(sq) != FILE_A) {
                                arg_ball->white.attack[sq + DIR_N - DIR_E] += 1;
                        }
                        break;

                case BLACK_PAWN:
                        (arg_ball->black).pawns[1+F(sq)] += 1;
                        if (F(sq) != FILE_H) {
                                (arg_ball->black).attack[sq - DIR_N + DIR_E] += 1;
                        }
                        if (F(sq) != FILE_A) {
                                (arg_ball->black).attack[sq - DIR_N - DIR_E] += 1;
                        }
                        break;
                }
        }
}

static void p_unmake_move(byte* p_board, ball*arg_ball)
{
        int sq;

        for (;;) {
                sq = *--arg_ball->undo_sp;
                if (sq < 0) break;               /* Found sentinel */
                p_board[sq] = *--arg_ball->undo_sp;
        }
        arg_ball->ply--;
}


static void p_make_move(int move, byte* p_board, ball *arg_ball)
{
        int fr;
        int to;
        int sq;

        *arg_ball->undo_sp++ = -1;                        /* Place sentinel */

        if (p_board[EP]) {                        /* Clear en-passant info */
                *arg_ball->undo_sp++ = p_board[EP];
                *arg_ball->undo_sp++ = EP;
                p_board[EP] = 0;
        }

        to = TO(move);
        fr = FR(move);

        if (move & SPECIAL) {                   /* Special moves first */
                switch (R(fr)) {
                case RANK_8:                    /* Black castles */
                        p_unmake_move(p_board, arg_ball);
                        if (to == G8) {
			  p_make_move(MOVE(H8,F8), p_board, arg_ball);
                        } else {
			  p_make_move(MOVE(A8,D8), p_board, arg_ball);
                        }
                        break;

                case RANK_7:
                        if (p_board[fr] == BLACK_PAWN) { /* Set en-passant flag */
                                *arg_ball->undo_sp++ = 0;
                                *arg_ball->undo_sp++ = EP;
                                p_board[EP] = to;
                        } else {                /* White promotes */
                                *arg_ball->undo_sp++ = p_board[fr];
                                *arg_ball->undo_sp++ = fr;
                                p_board[fr] = WHITE_QUEEN + (move>>13);
                        }
                        break;

                case RANK_5:                    /* White captures en-passant */
                case RANK_4:                    /* Black captures en-passant */
                        sq = SQ(F(to),R(fr));
                        *arg_ball->undo_sp++ = p_board[sq];
                        *arg_ball->undo_sp++ = sq;
                        p_board[sq] = EMPTY;
                        break;

                case RANK_2:
                        if (p_board[fr] == WHITE_PAWN) { /* Set en-passant flag */
                                *arg_ball->undo_sp++ = 0;
                                *arg_ball->undo_sp++ = EP;
                                p_board[EP] = to;
                        } else {                /* Black promotes */
                                *arg_ball->undo_sp++ = p_board[fr];
                                *arg_ball->undo_sp++ = fr;
                                p_board[fr] = BLACK_QUEEN + (move>>13);
                        }
                        break;

                case RANK_1:                    /* White castles */
                        p_unmake_move(p_board, arg_ball);
                        if (to == G1) {
			  p_make_move(MOVE(H1,F1), p_board, arg_ball);
                        } else {
			  p_make_move(MOVE(A1,D1), p_board, arg_ball);
                        }
                        break;

                default:
                        break;
                }
        }

        arg_ball->ply++;
        if (p_board[to]!=EMPTY ||
            p_board[fr]==WHITE_PAWN || p_board[fr]==BLACK_PAWN
        ) {
                *arg_ball->undo_sp++ = p_board[LAST];
                *arg_ball->undo_sp++ = LAST;
                p_board[LAST] = arg_ball->ply;
        }

        *arg_ball->undo_sp++ = p_board[to];
        *arg_ball->undo_sp++ = to;
        *arg_ball->undo_sp++ = p_board[fr];
        *arg_ball->undo_sp++ = fr;

        p_board[to] = p_board[fr];
        p_board[fr] = EMPTY;

        if (p_board[CASTLE] & (castle[fr] | castle[to])) {
                *arg_ball->undo_sp++ = p_board[CASTLE];
                *arg_ball->undo_sp++ = CASTLE;
                p_board[CASTLE] &= ~(castle[fr] | castle[to]);
        }
}

static int p_push_move(int fr, int to, byte* p_board, ball *arg_ball)
{
        unsigned short prescore = PRESCORE_EQUAL;
        int move;

        /* what do we capture */
        if (p_board[to] != EMPTY) {
                prescore += (1<<9) + prescore_piece_value[p_board[to]];
        }

        /* does the destination square look safe? */
        if (PWTM(arg_ball->ply)) {
                if ((arg_ball->black).attack[to] != 0) { /* defended */
                        prescore -= prescore_piece_value[p_board[fr]];
                }
        } else {
                if (arg_ball->white.attack[to] != 0) { /* defended */
                        prescore -= prescore_piece_value[p_board[fr]];
                }
        }

        if (prescore >= arg_ball->caps) {
                move = MOVE(fr, to);
                arg_ball->move_sp->move = move;

		/*SIMPLE history was removed*/
                arg_ball->move_sp->prescore = prescore /*| history[move]*/;
                arg_ball->move_sp++;
                return 1;
        }
        return 0;
}

static void p_push_special_move(int fr, int to, ball* arg_ball)
{
        int move;

        move = MOVE(fr, to);

	/*SIMPLE history was removed*/
        arg_ball->move_sp->prescore = PRESCORE_EQUAL /*| history[move]*/;
        arg_ball->move_sp->move = move | SPECIAL;
        arg_ball->move_sp++;
}

static void p_push_pawn_move(int fr, int to, byte* p_board, ball*arg_ball)
{
        if ((R(to) == RANK_8) || (R(to) == RANK_1)) {
	  p_push_special_move(fr, to, arg_ball);          /* queen promotion */
	  p_push_special_move(fr, to, arg_ball);          /* rook promotion */
                arg_ball->move_sp[-1].move += 1<<13;
                p_push_special_move(fr, to, arg_ball);          /* bishop promotion */
                arg_ball->move_sp[-1].move += 2<<13;
                p_push_special_move(fr, to, arg_ball);          /* knight promotion */
                arg_ball->move_sp[-1].move += 3<<13;
        } else {
	  p_push_move(fr, to, p_board, arg_ball);
        }
}


static void p_gen_slides(int fr, byte dirs, byte*p_board, ball *arg_ball)
{
        int vector;
        int to;
        byte dir = 0;

        dirs &= king_dirs[fr];
        do {
                dir -= dirs;
                dir &= dirs;
                vector = king_step[dir];
                to = fr;
                do {
                        to += vector;
                        if (p_board[to] != EMPTY) {
                                if (PIECE_COLOR(p_board[to]) != PWTM(arg_ball->ply)) {
				  p_push_move(fr, to, p_board, arg_ball);
                                }
                                break;
                        }
                        p_push_move(fr, to,p_board, arg_ball);
                } while (dir & king_dirs[to]);
        } while (dirs -= dir);
}


static int p_test_illegal(int move, byte* p_board, ball*arg_ball)
{
  p_make_move(move, p_board, arg_ball);
        p_compute_attacks(p_board, arg_ball);
        p_unmake_move(p_board, arg_ball);
        return arg_ball->friend->attack[arg_ball->enemy->king] != 0;
}


static void p_generate_moves(unsigned treshold, byte*p_board, ball*arg_ball)
{
        int             fr, to;
        int             pc;
        byte            dir, dirs;

        arg_ball->caps = treshold;

        for (fr=0; fr<64; fr++) {
                pc = p_board[fr];
                if (!pc || PIECE_COLOR(pc) != PWTM(arg_ball->ply)) continue;

                /*
                 *  generate moves for this piece
                 */
                switch (pc) {
                case WHITE_KING:
                case BLACK_KING:
                        dir = 0;
                        dirs = king_dirs[fr];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = fr+king_step[dir];
                                if (p_board[to] != EMPTY &&
                                    PIECE_COLOR(p_board[to]) == PWTM(arg_ball->ply)) continue;
                                p_push_move(fr, to, p_board, arg_ball);
                        } while (dirs -= dir);
                        break;

                case WHITE_QUEEN:
                case BLACK_QUEEN:
		  p_gen_slides(fr, ATK_SLIDER, p_board, arg_ball);
                        break;

                case WHITE_ROOK:
                case BLACK_ROOK:
		  p_gen_slides(fr, ATK_ORTHOGONAL, p_board, arg_ball);
                        break;

                case WHITE_BISHOP:
                case BLACK_BISHOP:
		  p_gen_slides(fr, ATK_DIAGONAL, p_board, arg_ball);
                        break;

                case WHITE_KNIGHT:
                case BLACK_KNIGHT:
                        dir = 0;
                        dirs = knight_dirs[fr];
                        do {
                                dir -= dirs;
                                dir &= dirs;
                                to = fr+knight_jump[dir];
                                if (p_board[to] != EMPTY &&
                                    PIECE_COLOR(p_board[to]) == PWTM(arg_ball->ply)) continue;
                                p_push_move(fr, to, p_board, arg_ball);
                        } while (dirs -= dir);
                        break;

                case WHITE_PAWN:
                        if (F(fr) != FILE_H) {
                                to = fr + DIR_N + DIR_E;
                                if (p_board[to] >= BLACK_KING) {
				  p_push_pawn_move(fr, to, p_board, arg_ball);
                                }
                        }
                        if (F(fr) != FILE_A) {
                                to = fr + DIR_N - DIR_E;
                                if (p_board[to] >= BLACK_KING) {
				  p_push_pawn_move(fr, to, p_board, arg_ball);
                                }
                        }
                        to = fr + DIR_N;
                        if (p_board[to] != EMPTY) {
                                break;
                        }
                        p_push_pawn_move(fr, to, p_board, arg_ball);
                        if (R(fr) == RANK_2) {
                                to += DIR_N;
                                if (p_board[to] == EMPTY) {
				  if (p_push_move(fr, to,p_board, arg_ball))
                                        if ((arg_ball->black).attack[to-DIR_N]) {
                                                arg_ball->move_sp[-1].move |= SPECIAL;
                                        }
                                }
                        }
                        break;

                case BLACK_PAWN:
                        if (F(fr) != FILE_H) {
                                to = fr - DIR_N + DIR_E;
                                if (p_board[to] && p_board[to] < BLACK_KING) {
				  p_push_pawn_move(fr, to, p_board, arg_ball);
                                }
                        }
                        if (F(fr) != FILE_A) {
                                to = fr - DIR_N - DIR_E;
                                if (p_board[to] && p_board[to] < BLACK_KING) {
				  p_push_pawn_move(fr, to, p_board, arg_ball);
                                }
                        }
                        to = fr - DIR_N;
                        if (p_board[to] != EMPTY) {
                                break;
                        }
                        p_push_pawn_move(fr, to, p_board, arg_ball);
                        if (R(fr) == RANK_7) {
                                to -= DIR_N;
                                if (p_board[to] == EMPTY) {
				  if (p_push_move(fr, to, p_board, arg_ball))
                                        if (arg_ball->white.attack[to+DIR_N]) {
                                                arg_ball->move_sp[-1].move |= SPECIAL;
                                        }
                                }
                        }
                        break;
                }
        }

        /*
         *  generate castling moves
         */
        if (p_board[CASTLE] && !arg_ball->enemy->attack[arg_ball->friend->king]) {
                if (PWTM(arg_ball->ply) && (p_board[CASTLE] & CASTLE_WHITE_KING) &&
                        !p_board[F1] && !p_board[G1] &&
                        !arg_ball->enemy->attack[F1])
                {
		  p_push_special_move(E1, G1, arg_ball);
                }
                if (PWTM(arg_ball->ply) && (p_board[CASTLE] & CASTLE_WHITE_QUEEN) &&
                        !p_board[D1] && !p_board[C1] && !p_board[B1] &&
                        !arg_ball->enemy->attack[D1])
                {
		  p_push_special_move(E1, C1, arg_ball);
                }
                if (!PWTM(arg_ball->ply) && (p_board[CASTLE] & CASTLE_BLACK_KING) &&
                        !p_board[F8] && !p_board[G8] &&
                        !arg_ball->enemy->attack[F8])
                {
		  p_push_special_move(E8, G8, arg_ball);
                }
                if (!PWTM(arg_ball->ply) && (p_board[CASTLE] & CASTLE_BLACK_QUEEN) &&
                        !p_board[D8] && !p_board[C8] && !p_board[B8] &&
                        !arg_ball->enemy->attack[D8])
                {
		  p_push_special_move(E8, C8, arg_ball);
                }
        }

        /*
         *  generate en-passant captures
         */
        if (p_board[EP]) {
                int ep = p_board[EP];

                if (PWTM(arg_ball->ply)) {
                        if (F(ep) != FILE_A && p_board[ep-DIR_E] == WHITE_PAWN) {
			  if (p_push_move(ep-DIR_E, ep+DIR_N, p_board, arg_ball))
                                        arg_ball->move_sp[-1].move |= SPECIAL;
                        }
                        if (F(ep) != FILE_H && p_board[ep+DIR_E] == WHITE_PAWN) {
			  if (p_push_move(ep+DIR_E, ep+DIR_N, p_board, arg_ball))
                                        arg_ball->move_sp[-1].move |= SPECIAL;
                        }
                } else {
                        if (F(ep) != FILE_A && p_board[ep-DIR_E] == BLACK_PAWN) {
			  if (p_push_move(ep-DIR_E, ep-DIR_N, p_board, arg_ball))
                                        arg_ball->move_sp[-1].move |= SPECIAL;
                        }
                        if (F(ep) != FILE_H && p_board[ep+DIR_E] == BLACK_PAWN) {
			  if (p_push_move(ep+DIR_E, ep-DIR_N, p_board, arg_ball))
                                        arg_ball->move_sp[-1].move |= SPECIAL;
                        }
                }
        }
}

/* Compute 32-bit Zobrist hash key. Normally 32 bits this is too small,
   but for MSCP's small searches it is OK */
static unsigned long p_compute_hash(byte* p_board, ball*arg_ball)
{
        unsigned long hash = 0;
        int sq;

        for (sq=0; sq<64; sq++) {
                if (p_board[sq] != EMPTY) {
                        hash ^= zobrist[p_board[sq]-1][sq];
                }
        }
        return hash ^ PWTM(arg_ball->ply);
}


static int p_evaluate(byte* p_board, ball*arg_ball)
{
        int sq;
        int score = 0;

        int white_has, black_has;

        /*
         *  stage 1: material+piece_square tables
         */
        for (sq=0; sq<64; sq++) {
                int file;

                if (p_board[sq] == EMPTY) continue;
                score += piece_square[p_board[sq]-1][sq];

                file = F(sq)+1;
                switch (p_board[sq]) {
                case WHITE_PAWN:
                {
                        int missing;
                        if (arg_ball->white.pawns[file] > 1) {
                                score -= 15;
                        }
                        missing = !arg_ball->white.pawns[file-1] + !arg_ball->white.pawns[file+1] +
                                !(arg_ball->black).pawns[file];
                        score -= missing * missing * 5;
                        break;
                }
                case BLACK_PAWN:
                {
                        int missing;
                        if ((arg_ball->black).pawns[file] > 1) {
                                score += 15;
                        }
                        missing = !(arg_ball->black).pawns[file-1] + !(arg_ball->black).pawns[file+1] +
                                !arg_ball->white.pawns[file];
                        score += missing * missing * 5;
                        break;
                }
                case WHITE_ROOK:
                        if (!arg_ball->white.pawns[file]) {
                                score += 10;
                                if (!(arg_ball->black).pawns[file]) {
                                        score += 10;
                                }
                        }
                        break;

                case BLACK_ROOK:
                        if (!(arg_ball->black).pawns[file]) {
                                score -= 10;
                                if (!arg_ball->white.pawns[file]) {
                                        score -= 10;
                                }
                        }
                        break;

                default:
                        break;
                }
        }

        /*
         *  stage 2: board control
         */
        white_has = 0;
        black_has = 0;
        for (sq=0; sq<64; sq++) {
                if (arg_ball->white.attack[sq] > (arg_ball->black).attack[sq]) {
                        white_has += white_control[sq];
                }
                if (arg_ball->white.attack[sq] < (arg_ball->black).attack[sq]) {
                        black_has += white_control[FLIP(sq)];
                }
        }
        score += (white_has - black_has);


	/*TEMP removed noise*/
	if(0){
        /* some noise to randomize play */
	  //        if (random_countdown>0){
	  score += (hash_stack[arg_ball->ply] ^ rnd_seed) % 17 - 8;
	}

        return PWTM(arg_ball->ply) ? score : -score;
}

static ball* setup(struct side * arg_white, struct side* arg_black, struct side* arg_friend, struct side* arg_enemy, int arg_ply, unsigned short arg_caps, struct move* copy_move_stack, int offset){
  ball*result=(ball*)malloc(sizeof(ball));
  result->white=(*arg_white);
  result->black=(*arg_black);
  result->ply=arg_ply;
  result->caps=arg_caps;
  //result->ply=ply;
  memcpy(copy_move_stack, move_stack, 1024*sizeof(struct move));
  result->move_sp=copy_move_stack+offset;
  result->undo_sp=(signed char*) malloc(6*1024*sizeof(signed char));
  result->nodes_visited=0;
  result->copy_move_stack=copy_move_stack;
  /*  if ((*((int*)(result->move_sp)))!=(*((int*)(move_sp)))){
    fprintf(stderr,"move_sp wasn't copied correctly\n");
  }
  else{
    fprintf(stderr,"YES, move_sp was copied correctly\n");
    }*/

  /*result->friend=arg_friend;
    result->enemy=arg_enemy;*/
    if(arg_black==arg_friend &&arg_white==arg_enemy){
    
    result->enemy=&(result->white);
    result->friend=&(result->black);
  }
  else if(arg_white==arg_friend &&arg_black==arg_enemy){
    
    result->enemy=&(result->black);
    result->friend=&(result->white);
  }
  else{
    
    } 

  return result;
}


static void ruin_global_variables(){
  move_sp+=999;
  friend+=999;
  enemy+=999;
  undo_sp+=999;
  ply+=999;
  caps+=999;
}

static void restore_global_variables(){
  move_sp-=999;
    friend-=999;
  enemy-=999;
  undo_sp-=999;
  ply-=999;
  caps-=999;
}


static int p_qsearch(int alpha, int beta, byte* p_board, ball *arg_ball)
{
        int                             best_score;
        int                             score;
        struct move                     *moves;

	/*SIMPLE reduced shared variables*/

        arg_ball->nodes_visited++;
	
	/*SIMPLE no hash
	  hash_stack[arg_ball->ply] = compute_hash();*/
        best_score = p_evaluate(p_board, arg_ball);
        if (best_score >= beta) {
                return best_score;
        }

        moves = arg_ball->move_sp;
        p_generate_moves(PRESCORE_EQUAL + (1<<9), p_board, arg_ball);
        qsort(moves, arg_ball->move_sp - moves, sizeof(*moves), cmp_move);
        while (arg_ball->move_sp > moves) {
                int move;

		//        arg_ball->nodes_visited++;

                arg_ball->move_sp--;
                move = arg_ball->move_sp->move;
                p_make_move(move, p_board, arg_ball);

                p_compute_attacks(p_board, arg_ball);
                if (arg_ball->friend->attack[arg_ball->enemy->king]) {
                        p_unmake_move(p_board, arg_ball);
                        continue;
                }

                score = - p_qsearch(-beta, -alpha, p_board, arg_ball);

                p_unmake_move(p_board, arg_ball);

                if (score <= best_score) {
                        continue;
                }
                best_score = score;

                if (score <= alpha) {
                        continue;
                }
                alpha = score;

                if (score < beta) {
                        continue;
                }
		arg_ball->move_sp = moves; /* fail high: skip remaining moves */
        }
        return best_score;
}




static int p_child_search(int depth, int alpha, int beta, byte* p_board, ball*arg_ball)
{
        int                             best_score = -INF;
        int                             best_move = 0;
        int                             score;
        struct move                     *moves;
        int                             incheck = 0;

        /*SIMPLE we cut these variables when we don't use the hash table
       
	struct tt                       *tt;
        int                             oldalpha = alpha;
	int                             oldbeta = beta;
        int                             i, count=0;
	*/

	
	//arg_ball->nodes_visited++;


	/*SIMPLE no hash stack
	  test for draw by repetition*/ 
        /*hash_stack[arg_ball->ply] = compute_hash();
        for (i=arg_ball->ply-4; i>=board[LAST]; i-=2) {
                if (hash_stack[i] == hash_stack[arg_ball->ply]) count++;
                if (count>=2) return 0;
		}*/

        
	/*  check transposition table*/
	/*
        tt = &TTABLE[ ((hash_stack[arg_ball->ply]>>16) & (CORE-1)) ];
        if (tt->hash == (hash_stack[arg_ball->ply] & 0xffffU)) {
                if (tt->depth >= depth) {
                        if (tt->flag >= 0) alpha = MAX(alpha, tt->score);
                        if (tt->flag <= 0) beta = MIN(beta,  tt->score);
                        if (alpha >= beta) return tt->score;
                }
                best_move = tt->move & 07777;
        }

        history[best_move] |= PRESCORE_HASHMOVE;*/
	
        incheck = arg_ball->enemy->attack[arg_ball->friend->king];
	
        /*
         *  p_generate moves
         */
        moves = arg_ball->move_sp;
        p_generate_moves(0, p_board, arg_ball);

        history[best_move] &= 0x3fff;
        best_move = 0;

        qsort(moves, arg_ball->move_sp - moves, sizeof(*moves), cmp_move);

        /*
         *  loop over all moves
         */
        while (arg_ball->move_sp > moves) {


                int newdepth;
                int move;
		//     	arg_ball->nodes_visited++;
                arg_ball->move_sp--;
                move = arg_ball->move_sp->move;
                p_make_move(move, p_board, arg_ball);
                p_compute_attacks(p_board, arg_ball);
                if (arg_ball->friend->attack[arg_ball->enemy->king]) {
                        p_unmake_move(p_board, arg_ball);
                        continue;
                }

                newdepth = incheck ? depth : depth-1;
                if (newdepth <= 0) {
		  /*TEMP should be deep copy of p_board? or not parallelized?*/
		  score = -p_qsearch(-beta, -alpha, p_board, arg_ball);
                } 

		/*	else if (newdepth>=2){

		  ball* arg_ball2;
		  byte* p_board2=(byte*) malloc(67*sizeof(byte));
		  int j;
		  struct move* move_stack_copy=(struct move*) malloc(1024*sizeof(struct move));
		  fprintf(stderr, "About to setup\n");

		  arg_ball2=setup(&(arg_ball->white), &(arg_ball->black), (arg_ball->friend), (arg_ball->enemy), arg_ball->ply, arg_ball->caps, move_stack_copy, arg_ball->move_sp-arg_ball->copy_move_stack);
		  


		  for (j=0; j<67; j++){
		    p_board2[j]=p_board[j];
		  }
		  
		  
		  score = cilk_spawn p_child_search(newdepth, -beta, -alpha, p_board2, arg_ball2);
		  score*=-1;
		  fprintf(stderr, "Finished with cilk_spawn\n");
		  
		
		//		  free(p_board2);
		//free(arg_ball2->undo_sp);
		// free(move_stack_copy);
		// free(arg_ball2);
		  //cilk_sync;		  		  
		  }*/

		else {

		  /*TEMP should be deep copy of p_board or note parallelized?*/
		  score = -p_child_search(newdepth, -beta, -alpha, p_board, arg_ball);
                }
                if (score < -29000) score++;    /* adjust for mate-in-n */

                p_unmake_move(p_board, arg_ball);

                if (score <= best_score) continue;
                best_score = score;
                best_move = move;

                if (score <= alpha) continue;
                alpha = score;

                if (score < beta) continue;

		arg_ball->move_sp = moves; /* fail high: skip remaining moves */
        }

        if (best_score == -INF) { /* deal with mate and stalemate */
                if (incheck) {
                        best_score = -30000;
                } else {
                        best_score = 0;
                }
        }




	/*	SIMPLE getting rid of hash table
       history[best_move & 07777] += depth*depth;
        if (history[best_move & 07777] > 511) {
                int m;
                for (m=0; m<SPECIAL; m++) {
		history[m] >>= 4;  */   /*  scaling */
	/*}
        }

        tt->hash = hash_stack[arg_ball->ply] & 0xffffU;
        tt->move = best_move;
        tt->score = best_score;
        tt->depth = depth;
        tt->flag = (oldalpha < best_score) - (best_score < oldbeta);
	*/
        return best_score;
}


static int cilk_child_search(int depth, int alpha, int beta, byte* p_board, ball*arg_ball, FILE*write_divergence){
  
        int                             best_score = -INF;
        int                             best_move = 0;
        int                             score;
        struct move                     *moves;
        int                             incheck = 0;
	int                             proceed=1;
	struct move *k;
	int fail=0;
	pthread_mutex_t main_lock;
	pthread_mutex_t nodes_visited_lock;
	pthread_mutex_t super_lock;


	pthread_mutex_init(&main_lock, NULL);
	pthread_mutex_init(&nodes_visited_lock, NULL);
	pthread_mutex_init(&super_lock, NULL);
        incheck = arg_ball->enemy->attack[arg_ball->friend->king];

        /*
         *  generate moves
         */
        moves = arg_ball->move_sp;
        p_generate_moves(0, p_board, arg_ball);
	
        history[best_move] &= 0x3fff;
        best_move = 0;
	
        qsort(moves, arg_ball->move_sp - moves, sizeof(*moves), cmp_move);

	//	for(k=arg_ball->move_sp; k>moves; k--){
	cilk_for(k=moves; k<=arg_ball->move_sp; k++){
	  //	while (arg_ball->move_sp > moves) {

	  int print_results=1;
	  double begin=currentSeconds();
	  double end;

	  int newdepth;
	  int move;
	  int go_on=1;
	  byte* p_board2=(byte*) malloc(67*sizeof(byte));
	  int j=0;
	  int local_alpha=0, local_beta=0;
	  int local_score;
	  //Make local copies of global variables
	  struct move* move_stack_copy;
	  ball*arg_ball2;

	  
	  //	(*nodes_visited)++;		
	  pthread_mutex_lock(&main_lock);
	  local_alpha=alpha;
	  local_beta=beta;
	  pthread_mutex_unlock(&main_lock);		  

	  //	pthread_mutex_lock(&main_lock);
	  if(fail){
	    
	    go_on=0;
	  }
	  //  pthread_mutex_unlock(&main_lock);		  
	  
	  if(go_on){
	    //pthread_mutex_lock(&super_lock);	    	    
	    move_stack_copy=(struct move*) malloc(1024*sizeof(struct move));
	    

	    arg_ball2=setup(&(arg_ball->white), &(arg_ball->black), arg_ball->friend, arg_ball->enemy, arg_ball->ply, arg_ball->caps, move_stack_copy, k-arg_ball->copy_move_stack);
	    memcpy(move_stack_copy, arg_ball->copy_move_stack, 1024*sizeof(struct move));
	    

	    for (j=0; j<67; j++){
	      p_board2[j]=p_board[j];
	    }
	    
	    /*TEMP used to prove that global variable isn't being touched.*/
	    //		ruin_global_variables();
	    
	    
	    
	    
	    
	    //		move_sp--;
	    (arg_ball2->move_sp)--;
	    move = arg_ball2->move_sp->move;
	    
	    /*TEMP this should be deep copy of board*/
	    
	    p_make_move(move, p_board2, arg_ball2);
	    
	    /*TEMP this needs to be a deep copy of board*/
	    p_compute_attacks(p_board2, arg_ball2);
	    if (arg_ball2->friend->attack[arg_ball2->enemy->king]) {
	      
	      
	      /*TEMP this should be a deep copy of board*/
	      p_unmake_move(p_board2, arg_ball2);
	      
	      /*TEMP eliminating frees*/
		free(move_stack_copy);
		free(arg_ball2->undo_sp);
		free(arg_ball2);
		free(p_board2 );
	      
	      /*TEMP used to prove that global variable isn't being touched.*/
	      //restore_global_variables();
	      

	      //	      pthread_mutex_unlock(&super_lock);
	      go_on=0;
	    }
	  }
	  
	  if(go_on){
	    //	    	    pthread_mutex_unlock(&super_lock);
	    //pthread_mutex_lock(&super_lock);
	    newdepth = incheck ? depth : depth-1;
	    if (newdepth <= 0) {
	      
	      /*TEMP this should be a deep copy of board*/
		    
	      local_score = -p_qsearch(-local_beta, -local_alpha, p_board2, arg_ball2);
	    } 
	    else if( (newdepth>=CILK_THRESHOLD)&& WORK_STEAL_B){
	      local_score=-cilk_child_search(newdepth, -local_beta, -local_alpha, p_board2, arg_ball2, write_divergence);
	      print_results=0;
		    }
	    
	    else{
	      /*TEMP this should be deep copy of p_board*/
	      local_score = -p_child_search(newdepth, -local_beta, -local_alpha, p_board2, arg_ball2);
	    }



	    if (local_score < -29000) local_score++;    /* adjust for mate-in-n */
	    
	    
	    pthread_mutex_lock(&nodes_visited_lock);
	    (arg_ball->nodes_visited)+=arg_ball2->nodes_visited;
	    
	    pthread_mutex_unlock(&nodes_visited_lock);
	    
	    
	    /*TEMP this should be a deep copy of board*/
	    p_unmake_move(p_board2, arg_ball2);
	    
	    
	    /*TEMP eliminating frees*/
	      free(move_stack_copy);
	      free(arg_ball2->undo_sp);
	      free(arg_ball2);
	      free(p_board2);
	    /*}
	      
	      if(go_on){*/
	    
	    
	    if ((local_score>alpha) || (local_score>best_score)){
	      pthread_mutex_lock(&main_lock);
	      if (local_score>alpha){
		usleep(1);
		best_score = local_score;
		best_move = move;
		alpha = local_score;
	      }
	      
	      else if (local_score>best_score){
		usleep(1);
		best_score = local_score;
		best_move = move;   
	      }
	      pthread_mutex_unlock(&main_lock);
	    }
	    
	    if (local_score>beta){
	      fail=1;
	      
	    }
	    //	  pthread_mutex_unlock(&super_lock);   	    
	  }//end go_on
	  
	  
	  end=currentSeconds();
	  if(print_results)fprintf(write_divergence, "%d %f\n",__cilkrts_get_worker_number(), end-begin);

	} //for/cilk_for
	arg_ball->move_sp=moves;
	
	
        if (best_score == -INF) { /* deal with mate and stalemate */
	  if (incheck) {
	    best_score = -30000;
	  } else {
	    best_score = 0;
	  }
        }
	
        return best_score;
}

static int p_vsearch(int depth, int alpha, int beta, int* nodes_visited, FILE*write_divergence)
{
        int                             best_score = -INF;
        int                             best_move = 0;
        int                             score;
        struct move                     *moves;
        int                             incheck = 0;
	int                             proceed=1;
	struct move *k;
	int fail=0;
	pthread_mutex_t main_lock;
	
	pthread_mutex_t nodes_visited_lock;

        /*SIMPLE we cut these variables when we don't use the hash table
       
	struct tt                       *tt;
        int                             oldalpha = alpha;
	int                             oldbeta = beta;
        int                             i, count=0;
	*/

	/*SIMPLE removed this to avoid shared variables
        nodes++;
	*/



	/*SIMPLE no hash stack
	  test for draw by repetition*/ 
        /*hash_stack[ply] = compute_hash();
        for (i=ply-4; i>=board[LAST]; i-=2) {
                if (hash_stack[i] == hash_stack[ply]) count++;
                if (count>=2) return 0;
		}*/

        
	/*  check transposition table*/
	/*
        tt = &TTABLE[ ((hash_stack[ply]>>16) & (CORE-1)) ];
        if (tt->hash == (hash_stack[ply] & 0xffffU)) {
                if (tt->depth >= depth) {
                        if (tt->flag >= 0) alpha = MAX(alpha, tt->score);
                        if (tt->flag <= 0) beta = MIN(beta,  tt->score);
                        if (alpha >= beta) return tt->score;
                }
                best_move = tt->move & 07777;
        }

        history[best_move] |= PRESCORE_HASHMOVE;*/


	pthread_mutex_init(&main_lock, NULL);
	
pthread_mutex_init(&nodes_visited_lock, NULL);
        incheck = enemy->attack[friend->king];

        /*
         *  generate moves
         */
        moves = move_sp;
        generate_moves(0);

        history[best_move] &= 0x3fff;
        best_move = 0;

        qsort(moves, move_sp - moves, sizeof(*moves), cmp_move);


	/*recursively call this function*/
        while (proceed && (move_sp > moves)) {
		
                int newdepth;
                int move;
			(*nodes_visited)++;		
	
                move_sp--;
                move = move_sp->move;
                make_move(move);
                compute_attacks();
                if (friend->attack[enemy->king]) {
                        unmake_move();
                        continue;
                }

                newdepth = incheck ? depth : depth-1;
                if (newdepth <= 0) {
		  score = -qsearch(-beta, -alpha);
                } else {
		  score = -p_vsearch(newdepth, -beta, -alpha, nodes_visited, write_divergence);
                }
                if (score < -29000) score++;    /* adjust for mate-in-n */
		proceed=0;
                unmake_move();

                if (score <= best_score) continue;
                best_score = score;
                best_move = move;

                if (score <= alpha) continue;
                alpha = score;

                if (score < beta) continue;

		move_sp = moves; /* fail high: skip remaining moves */
	}



        /*
         *  loop over all moves
         */
	parallel_code=1;
	       
	//	for(k=move_sp; k>moves; k--){
	cilk_for(k=moves+1; k<=move_sp; k++){
	  //	while (move_sp > moves) {
	  
	  int print_results=1;
	  double begin=currentSeconds();
	  double end;
	  int newdepth;
	  int move;
	  int go_on=1;
	  byte* p_board=(byte*) malloc(67*sizeof(byte));
	  int j=0;
	  int local_alpha=0, local_beta=0;
	  int local_score;
		//Make local copies of global variables
	  struct move* move_stack_copy;
	  ball*arg_ball;
	  
	  
	  //	(*nodes_visited)++;		
	  pthread_mutex_lock(&main_lock);
	  local_alpha=alpha;
	  local_beta=beta;
	  pthread_mutex_unlock(&main_lock);		  

		  //	pthread_mutex_lock(&main_lock);
		if(fail){

		  go_on=0;
		}
		//  pthread_mutex_unlock(&main_lock);		  
		  if(go_on){
		move_stack_copy=(struct move*) malloc(1024*sizeof(struct move));
		arg_ball=setup(&white, &black, friend, enemy, ply, caps, move_stack_copy, k-move_stack);
		for (j=0; j<67; j++){
		  p_board[j]=board[j];
		}

		/*TEMP used to prove that global variable isn't being touched.*/
		//		ruin_global_variables();
		

		

		
		//		move_sp--;
                (arg_ball->move_sp)--;
                move = arg_ball->move_sp->move;

		/*TEMP this should be deep copy of board*/

                p_make_move(move, p_board, arg_ball);

		/*TEMP this needs to be a deep copy of board*/
                p_compute_attacks(p_board, arg_ball);
                if (arg_ball->friend->attack[arg_ball->enemy->king]) {


		  /*TEMP this should be a deep copy of board*/
                        p_unmake_move(p_board, arg_ball);
			
			/*TEMP eliminating frees*/
			free(move_stack_copy);
			free(arg_ball->undo_sp);
			free(arg_ball);
			free(p_board );

		/*TEMP used to prove that global variable isn't being touched.*/
			//restore_global_variables();
			
			go_on=0;
                }
		  }

		if(go_on){
		  newdepth = incheck ? depth : depth-1;
		  if (newdepth <= 0) {
		    
		    /*TEMP this should be a deep copy of board*/
		    
		    local_score = -p_qsearch(-local_beta, -local_alpha, p_board, arg_ball);
		  } 
		  
		  else if ((newdepth>=CILK_THRESHOLD)&&WORK_STEAL_A){
		    local_score=-cilk_child_search(newdepth, -local_beta, -local_alpha, p_board, arg_ball, write_divergence);
		    print_results=0;
		    }
		  else{
		    /*TEMP this should be deep copy of p_board*/
		    local_score = -p_child_search(newdepth, -local_beta, -local_alpha, p_board, arg_ball);
		  }
		  if (local_score < -29000) local_score++;    /* adjust for mate-in-n */


		  pthread_mutex_lock(&nodes_visited_lock);
		  (*nodes_visited)+=arg_ball->nodes_visited;
		  
		  pthread_mutex_unlock(&nodes_visited_lock);
		  
		  
		  /*TEMP this should be a deep copy of board*/
		  p_unmake_move(p_board, arg_ball);
		  

		  /*TEMP eliminating frees*/
		  free(move_stack_copy);
		  free(arg_ball->undo_sp);
		  free(arg_ball);
		  free(p_board);
		  /*}
		
		    if(go_on){*/

		  
		  if ((local_score>alpha) || (local_score>best_score)){
		    pthread_mutex_lock(&main_lock);
		    if (local_score>alpha){
		      usleep(1);
		      best_score = local_score;
		      best_move = move;
		      alpha = local_score;
		    }
		    
		    else if (local_score>best_score){
		      usleep(1);
		      best_score = local_score;
		      best_move = move;   
		    }
		    	pthread_mutex_unlock(&main_lock);
		  }

		  if (local_score>beta){
		      fail=1;
		    
		  }

		}//end go_on
	

		end=currentSeconds();

		if(print_results)
		  fprintf(write_divergence, "%d %f\n",__cilkrts_get_worker_number(), end-begin);    
		/*		if(go_on){

		  
		  if (score < beta) go_on=0;
		}//end go_on
		
		if (go_on){*/
		  //TEMP: we're eliminating this optimization to be more cilk compliant
		  //		  k=move_sp+1;
		  //	k=moves;
		  //move_sp = moves; /* fail high: skip remaining moves */
		//end go_on


        }
	parallel_code=0;
	
	move_sp=moves;


        if (best_score == -INF) { /* deal with mate and stalemate */
                if (incheck) {
                        best_score = -30000;
                } else {
                        best_score = 0;
                }
        }




	/*	SIMPLE getting rid of hash table
       history[best_move & 07777] += depth*depth;
        if (history[best_move & 07777] > 511) {
                int m;
                for (m=0; m<SPECIAL; m++) {
		history[m] >>= 4;  */   /*  scaling */
	/*}
        }

        tt->hash = hash_stack[arg_ball->ply] & 0xffffU;
        tt->move = best_move;
        tt->score = best_score;
        tt->depth = depth;
        tt->flag = (oldalpha < best_score) - (best_score < oldbeta);
	*/
        return best_score;
}


void array_compare(byte* arr1, byte* arr2, int len, char* message){
  int proceed=1;
  int j=0;
  while ((j<len) && proceed){
    if (arr1[j]!=arr2[j]){
	proceed=0;
	
      }
    j++;

  }

}

static int p_root_search(int maxdepth, FILE* write_time, FILE*write_first_time, FILE*write_second_time, FILE*write_count, FILE*write_first_count, FILE*write_second_count, FILE* write_divergence)
{
  int             depth;
  int             score, best_score;
  int             move = 0;
  int             alpha, beta;
  unsigned long   node;
  struct move     *m;
  pthread_mutex_t main_lock;
  pthread_mutex_t nodes_visited_lock;
  pthread_mutex_t super_lock;
  int num_moves=0;
  double initialize,top, mid, bottom;
  int top_nodes, mid_nodes, bottom_nodes;
  int *nodes_visited=(int*)malloc(sizeof(int));
  

  initialize=currentSeconds();

  //  fprintf(write_divergence, "blam 0");
  pthread_mutex_init (&main_lock, NULL);
pthread_mutex_init (&nodes_visited_lock, NULL);

pthread_mutex_init (&super_lock, NULL);  
  nodes = 0;
  compute_piece_square_tables();
  
  generate_moves(0);
  qsort(move_stack, move_sp-move_stack, sizeof(struct move), cmp_move);
  
  alpha = -INF;
  beta = +INF;
  *nodes_visited=0;
  puts(" nodes ply score move");
  
  
  
  
  
  
  for (depth = 1; depth <= maxdepth; depth++) {
    int proceed=1;
    top=currentSeconds();
    top_nodes=*nodes_visited;
    m = move_stack;
    best_score = INT_MIN;
    
    node = nodes;
    num_moves=0;
    
    /*in parallel version the first iteration works different than subsequent iterations.*/
    while (m < move_sp && proceed) {
      
      /*go into move, check if legal;*/
      make_move(m->move);
      compute_attacks();
      if (friend->attack[enemy->king] != 0) { /* illegal? */

	unmake_move();
	*m = *--move_sp; /* drop this move */
	continue;
      }
      
      
      /*			 SIMPLE No hash stack
      //don't know what this is anyway
      hash_stack[ply] = compute_hash();*/
      
      
      /*do normal search. Or if end of depth, Q-Search*/
      if (depth-1 > 0) {
	
	/*TEMP change*/
	score = -p_vsearch(depth-1, -beta, -alpha, nodes_visited, write_divergence);
      } else {
	
	score = -qsearch(-beta, -alpha);
      }
      unmake_move();
      
      
      /*Fix window if it was too narrow.*/
      if (score>=beta || (score<=alpha && m==move_stack)) {
	alpha = -INF;
	beta = +INF;
	continue; /* re-search this move */
      }
      
      /*I don't know what this is*/
      m->prescore = ~squeeze(nodes-node);
      node = nodes;

      num_moves++;
      /*            if((ply==147 ) && depth==1){
	  printf("B DEBUG %5lu %3d %+1.2f ", nodes, depth, score / 100.0);
	  print_move_san(m->move);
	  puts("");
	  }*/



      
      if ((score > best_score) || (score==best_score &&(m->move>move))) {
	      //if (score > best_score) {
	struct move tmp;
	
	best_score = score;
	alpha = score-1;
	beta = score + 2;
	move = m->move;
	
	tmp = *move_stack; /* swap with top of list */
	*move_stack = *m;
	*m = tmp;
      }
      m++; /* continue with next move */
      proceed=0;		
    }
    

    if(depth>=maxdepth-1){
    mid=currentSeconds();
    mid_nodes=*nodes_visited;
    fprintf(stderr, "Depth %d: First half time %f nodes is %d\n", depth, mid-top, mid_nodes-top_nodes);
    fprintf(write_first_time, "%f\n", mid-top);
    fprintf(write_first_count, "%d\n", mid_nodes-top_nodes);
    //fprintf(write_divergence, "0 %f\n", 4*(mid-top));
    }

    parallel_code=1;
    //for(m=move_sp-1; m>=move_stack +1; m--){
      cilk_for (m=move_stack+1;m < move_sp;m++) {
      //fprintf(stderr,"move_stack was %d, m was %d and move_sp was %d\n", move_stack, m, move_sp);
	
	double begin=currentSeconds();
	double end;
	int leave_loop=0;
	int local_score;
	/*      byte*p_board=board;*/
	
	while(leave_loop==0){

	  pthread_mutex_lock(&main_lock);
	  int local_alpha=alpha;
	  int local_beta=beta;
	  pthread_mutex_unlock(&main_lock);
	  //pthread_mutex_lock (&super_lock);
	  leave_loop=1;
	  struct move* move_stack_copy=(struct move*) malloc(1024*sizeof(struct move));
      
	  ball*arg_ball=setup(&white, &black, friend, enemy, ply, caps, move_stack_copy, (move_sp-move_stack));
      //arg_ball->move_sp=move_sp;

	  int j=0;
	  byte* p_board=(byte*) malloc(67*sizeof(byte));
	  for (j=0;j<67; j++){
	    p_board[j]=board[j];
	  }
      //ruin_global_variables();
      pthread_mutex_lock(& nodes_visited_lock);
(*nodes_visited)++;
pthread_mutex_unlock(& nodes_visited_lock);
      /*go into move, check if legal;*/
      
      /*TEMP this should be a deep copy of board*/
      p_make_move(m->move, p_board, arg_ball);
      
      /*TEMP this needs to be a deep copy of board.*/
      p_compute_attacks(p_board, arg_ball);
      if (arg_ball->friend->attack[arg_ball->enemy->king] != 0) { /* illegal? */
	
	/*TEMP this needs to be a deep copy of board.*/
	p_unmake_move(p_board, arg_ball);


	//	move_sp--;
	//	*m = *--(arg_ball->move_sp); /* drop this move */
	//m--;	

	//move_sp=arg_ball->move_sp;
	free(move_stack_copy);
	free(p_board );
	free(arg_ball->undo_sp);
	free(arg_ball);
	//pthread_mutex_unlock (&super_lock);
	//restore_global_variables();
	continue;
      }

      
      /*			 SIMPLE No hash stack
      //don't know what this is anyway
      hash_stack[arg_ball->ply] = p_compute_hash(p_board, arg_ball);*/
      
      
      /*do normal search. Or if end of depth, Q-Search*/
	
      if (depth-1 > 0) {
	
	/*TEMP  This needs to be deep copy of board*/
	
	local_score =  p_child_search(depth-1, -local_beta, -local_alpha, p_board, arg_ball);
	local_score*=-1;
      } else {
	

	/*TEMP  This needs to be deep copy of board*/
	local_score = -p_qsearch(-local_beta, -local_alpha, p_board, arg_ball);
      }


      /*TEMP  This needs to be deep copy of board*/
      p_unmake_move(p_board, arg_ball);
      

	pthread_mutex_lock(& nodes_visited_lock);

	(*nodes_visited)+=arg_ball->nodes_visited;
	pthread_mutex_unlock(& nodes_visited_lock);


      //      move_sp=arg_ball->move_sp;
      free(move_stack_copy);
      free(arg_ball->undo_sp);
      free(arg_ball);
      free(p_board );
      //restore_global_variables();

      
      /*Fix window if it was too narrow.*/
      if (local_score>=local_beta || (local_score<=alpha && m==move_stack)) {
	pthread_mutex_lock(& main_lock);
	if (local_score>=beta || (local_score<=alpha && m==move_stack)) {
	alpha = -INF;
	beta = +INF;
	}
	pthread_mutex_unlock(& main_lock);
	//	m--;
	leave_loop=0;
	//pthread_mutex_unlock (&super_lock);
	continue; // re-search this move 
      }
      


      /*I don't know what this is*/
      /*m->prescore = ~squeeze(nodes-node);
	node = nodes;*/


      num_moves++;
      /*            if((ply==147 ) && depth==1){
	  printf("C DEBUG %5lu %3d %+1.2f ", nodes, depth, local_score / 100.0);
	  print_move_san(m->move);
	  puts("");
	  }*/
    
      pthread_mutex_lock(& main_lock);
      if ((local_score > best_score) || (local_score==best_score &&(m->move>move))) {
	struct move tmp;
	
	best_score = local_score;
	alpha = local_score-1;
	beta = local_score + 2;
	move = m->move;
	
	tmp = *move_stack; // swap with top of list 
	*move_stack = *m;
	*m = tmp;
      }
      pthread_mutex_unlock(& main_lock);

      //      pthread_mutex_unlock (&super_lock);            
      }//while proceed
  
      end=currentSeconds();
      
            fprintf(write_divergence, "%d %f\n",__cilkrts_get_worker_number(), end-begin);    

       /* continue with next move */
    }
    parallel_code=0;
    
    
    /*    if ((num_moves<=1) ||(move_sp-move_stack <= 1)) {
      break; // just one move to play 
      }*/
        printf(" %3d %+1.2f ", depth, best_score / 100.0);
    print_move_san(move);
    puts("");
    



    /* sort remaining moves in descending order of subtree size */
    //SIMPLE this relies on the hash table we don't use
    //    qsort(move_stack+1, move_sp-move_stack-1, sizeof(*m), cmp_move);
    
    /*Widen window for deeper search*/
    alpha = best_score - 33;        /* aspiration window */
    beta = best_score + 33;


    if(depth>=maxdepth-1){
      bottom=currentSeconds();
      bottom_nodes=*nodes_visited;
      fprintf(stderr, "Depth %d: second half time %f and nodes %d\n", depth, bottom-mid, bottom_nodes-mid_nodes);
      total_nodes_visited+=(*nodes_visited);
      fprintf(stderr, "Nodes visited here was %d, total was %d\n", *nodes_visited,total_nodes_visited);
      fprintf(write_second_time, "%f\n", bottom-mid);
      fprintf(write_second_count, "%d\n", bottom_nodes-mid_nodes);
  
    }


  }
  move_sp = move_stack;

  bottom=currentSeconds();
  fprintf(stderr, "Total time was was %f\n", bottom-initialize);
  fprintf(write_count, "%d\n", *nodes_visited);
  fprintf(write_divergence, "fence %f\n",/*__cilkrts_get_worker_number(),*/ bottom-initialize);


  return move;
}

static int p_root_time(int maxdepth, FILE* write_time, FILE*write_first_time, FILE*write_second_time, FILE*write_count, FILE*write_first_count, FILE*write_second_count, FILE* write_divergence)
{
  int             depth;
  int             score, best_score;
  int             move = 0;
  int             result_move;
  int             alpha, beta;
  unsigned long   node;
  struct move     *m;
  pthread_mutex_t main_lock;
  pthread_mutex_t nodes_visited_lock;
  pthread_mutex_t super_lock;
  int num_moves=0;
  double initialize,top, mid, bottom;
  int top_nodes, mid_nodes, bottom_nodes;
  int *nodes_visited=(int*)malloc(sizeof(int));
  

  initialize=currentSeconds();

  //  fprintf(write_divergence, "blam 0");
  pthread_mutex_init (&main_lock, NULL);
pthread_mutex_init (&nodes_visited_lock, NULL);

pthread_mutex_init (&super_lock, NULL);  
  nodes = 0;
  compute_piece_square_tables();
  
  generate_moves(0);
  qsort(move_stack, move_sp-move_stack, sizeof(struct move), cmp_move);
  
  alpha = -INF;
  beta = +INF;
  *nodes_visited=0;
  puts(" nodes ply score move");
  
  
  
  
  
  
  for (depth = 1; (depth<=1) || (currentSeconds()-initialize<TIME_LIMIT); depth++) {
    int proceed=1;
    global_depth=depth-1;
    top=currentSeconds();
    top_nodes=*nodes_visited;
    m = move_stack;
    best_score = INT_MIN;
    
    node = nodes;
    num_moves=0;


    result_move=move;
    
    /*in parallel version the first iteration works different than subsequent iterations.*/
    while (m < move_sp && proceed) {
      
      /*go into move, check if legal;*/
      make_move(m->move);
      compute_attacks();
      if (friend->attack[enemy->king] != 0) { /* illegal? */

	unmake_move();
	*m = *--move_sp; /* drop this move */
	continue;
      }
      
      
      /*			 SIMPLE No hash stack
      //don't know what this is anyway
      hash_stack[ply] = compute_hash();*/
      
      
      /*do normal search. Or if end of depth, Q-Search*/
      if (depth-1 > 0) {
	
	/*TEMP change*/
	score = -p_vsearch(depth-1, -beta, -alpha, nodes_visited, write_divergence);
      } else {
	
	score = -qsearch(-beta, -alpha);
      }
      unmake_move();
      
      
      /*Fix window if it was too narrow.*/
      if (score>=beta || (score<=alpha && m==move_stack)) {
	alpha = -INF;
	beta = +INF;
	continue; /* re-search this move */
      }
      
      /*I don't know what this is*/
      m->prescore = ~squeeze(nodes-node);
      node = nodes;

      num_moves++;
      /*            if((ply==147 ) && depth==1){
	  printf("B DEBUG %5lu %3d %+1.2f ", nodes, depth, score / 100.0);
	  print_move_san(m->move);
	  puts("");
	  }*/



      
      if ((score > best_score) || (score==best_score &&(m->move>move))) {
	      //if (score > best_score) {
	struct move tmp;
	
	best_score = score;
	alpha = score-1;
	beta = score + 2;
	move = m->move;
	
	tmp = *move_stack; /* swap with top of list */
	*move_stack = *m;
	*m = tmp;
      }
      m++; /* continue with next move */
      proceed=0;		
    }
    

    if(depth>=maxdepth-1){
    mid=currentSeconds();
    mid_nodes=*nodes_visited;
    /*fprintf(stderr, "Depth %d: First half time %f nodes is %d\n", depth, mid-top, mid_nodes-top_nodes);
    fprintf(write_first_time, "%f\n", mid-top);
    fprintf(write_first_count, "%d\n", mid_nodes-top_nodes);*/
    //fprintf(write_divergence, "0 %f\n", 4*(mid-top));
    }

    parallel_code=1;
    //for(m=move_sp-1; m>=move_stack +1; m--){
      cilk_for (m=move_stack+1;m < move_sp;m++) {
      //fprintf(stderr,"move_stack was %d, m was %d and move_sp was %d\n", move_stack, m, move_sp);
	
	double begin=currentSeconds();
	double end;
	int leave_loop=0;
	int local_score;
	/*      byte*p_board=board;*/
	
	while(leave_loop==0){

	  pthread_mutex_lock(&main_lock);
	  int local_alpha=alpha;
	  int local_beta=beta;
	  pthread_mutex_unlock(&main_lock);
	  //pthread_mutex_lock (&super_lock);
	  leave_loop=1;
	  struct move* move_stack_copy=(struct move*) malloc(1024*sizeof(struct move));
      
	  ball*arg_ball=setup(&white, &black, friend, enemy, ply, caps, move_stack_copy, (move_sp-move_stack));
      //arg_ball->move_sp=move_sp;

	  int j=0;
	  byte* p_board=(byte*) malloc(67*sizeof(byte));
	  for (j=0;j<67; j++){
	    p_board[j]=board[j];
	  }
      //ruin_global_variables();
      pthread_mutex_lock(& nodes_visited_lock);
(*nodes_visited)++;
pthread_mutex_unlock(& nodes_visited_lock);
      /*go into move, check if legal;*/
      
      /*TEMP this should be a deep copy of board*/
      p_make_move(m->move, p_board, arg_ball);
      
      /*TEMP this needs to be a deep copy of board.*/
      p_compute_attacks(p_board, arg_ball);
      if (arg_ball->friend->attack[arg_ball->enemy->king] != 0) { /* illegal? */
	
	/*TEMP this needs to be a deep copy of board.*/
	p_unmake_move(p_board, arg_ball);


	//	move_sp--;
	//	*m = *--(arg_ball->move_sp); /* drop this move */
	//m--;	

	//move_sp=arg_ball->move_sp;
	free(move_stack_copy);
	free(p_board );
	free(arg_ball->undo_sp);
	free(arg_ball);
	//pthread_mutex_unlock (&super_lock);
	//restore_global_variables();
	continue;
      }

      
      /*			 SIMPLE No hash stack
      //don't know what this is anyway
      hash_stack[arg_ball->ply] = p_compute_hash(p_board, arg_ball);*/
      
      
      /*do normal search. Or if end of depth, Q-Search*/
	
      if (depth-1 > 0) {
	
	/*TEMP  This needs to be deep copy of board*/
	
	local_score =  p_child_search(depth-1, -local_beta, -local_alpha, p_board, arg_ball);
	local_score*=-1;
      } else {
	

	/*TEMP  This needs to be deep copy of board*/
	local_score = -p_qsearch(-local_beta, -local_alpha, p_board, arg_ball);
      }


      /*TEMP  This needs to be deep copy of board*/
      p_unmake_move(p_board, arg_ball);
      

	pthread_mutex_lock(& nodes_visited_lock);

	(*nodes_visited)+=arg_ball->nodes_visited;
	pthread_mutex_unlock(& nodes_visited_lock);


      //      move_sp=arg_ball->move_sp;
      free(move_stack_copy);
      free(arg_ball->undo_sp);
      free(arg_ball);
      free(p_board );
      //restore_global_variables();

      
      /*Fix window if it was too narrow.*/
      if (local_score>=local_beta || (local_score<=alpha && m==move_stack)) {
	pthread_mutex_lock(& main_lock);
	if (local_score>=beta || (local_score<=alpha && m==move_stack)) {
	alpha = -INF;
	beta = +INF;
	}
	pthread_mutex_unlock(& main_lock);
	//	m--;
	leave_loop=0;
	//pthread_mutex_unlock (&super_lock);
	continue; // re-search this move 
      }
      


      /*I don't know what this is*/
      /*m->prescore = ~squeeze(nodes-node);
	node = nodes;*/


      num_moves++;
      /*            if((ply==147 ) && depth==1){
	  printf("C DEBUG %5lu %3d %+1.2f ", nodes, depth, local_score / 100.0);
	  print_move_san(m->move);
	  puts("");
	  }*/
    
      pthread_mutex_lock(& main_lock);
      if ((local_score > best_score) || (local_score==best_score &&(m->move>move))) {
	struct move tmp;
	
	best_score = local_score;
	alpha = local_score-1;
	beta = local_score + 2;
	move = m->move;
	
	tmp = *move_stack; // swap with top of list 
	*move_stack = *m;
	*m = tmp;
      }
      pthread_mutex_unlock(& main_lock);

      //      pthread_mutex_unlock (&super_lock);            
      }//while proceed
  
      end=currentSeconds();
      
      /*      fprintf(write_divergence, "%d %f\n",__cilkrts_get_worker_number(), end-begin);    */

       /* continue with next move */
    }
    parallel_code=0;
    
    
    /*    if ((num_moves<=1) ||(move_sp-move_stack <= 1)) {
      break; // just one move to play 
      }*/
    /*printf(" %3d %+1.2f ", depth, best_score / 100.0);
    print_move_san(move);
    puts("");*/
    
    /* sort remaining moves in descending order of subtree size */
    //SIMPLE this relies on the hash table we don't use
    //    qsort(move_stack+1, move_sp-move_stack-1, sizeof(*m), cmp_move);
    
    /*Widen window for deeper search*/
    alpha = best_score - 33;        /* aspiration window */
    beta = best_score + 33;


    if(depth>=maxdepth-1){
      bottom=currentSeconds();
      bottom_nodes=*nodes_visited;
      //fprintf(stderr, "Depth %d: second half time %f and nodes %d\n", depth, bottom-mid, bottom_nodes-mid_nodes);
      total_nodes_visited+=(*nodes_visited);
      /*fprintf(stderr, "Nodes visited here was %d, total was %d\n", *nodes_visited,total_nodes_visited);
      fprintf(write_second_time, "%f\n", bottom-mid);
      fprintf(write_second_count, "%d\n", bottom_nodes-mid_nodes);*/
  
    }


  }
  move_sp = move_stack;

  bottom=currentSeconds();
  /*  fprintf(stderr, "Total time was was %f\n", bottom-initialize);
      fprintf(write_count, "%d\n", *nodes_visited);*/
  //  fprintf(write_divergence, "fence %f\n",/*__cilkrts_get_worker_number(),*/ bottom-initialize);


  return result_move;
}


/*----------------------------------------------------------------------+
 |      main                                                            |
 +----------------------------------------------------------------------*/

static void catch_sigint(int s)
{
        signal(s, catch_sigint);
}

static char startup_message[] =
        "\n"
        "This is MSCP 1.4 (Marcel's Simple Chess Program)\n"
        "\n"
        "Copyright (C)1998-2003 Marcel van Kervinck\n"
        "This program is distributed under the GNU General Public License.\n"
        "(See file COPYING or http://combinational.com/mscp/ for details.)\n"
        "\n"
        "Type 'help' for a list of commands\n";

int main(int argc, char *argv[])
{
        int i;
        int cmd;
        char line[128];
        char name[128];
	char filename[256];
        int move;
	clock_t start, end;
	double start_c, end_c;
	FILE*write_time, *write_first_time, *write_second_time, *write_first_count, *write_second_count, *write_count, *write_divergence, *write_real_white, *write_real_black;


	if (argc>2 && atoi(argv[2])==1){
	  sprintf(filename, "%s%stime2.dat", argv[5],argv[6]);
	  write_time=fopen(filename, "w");
	  sprintf(filename, "%s%stime_first2.dat", argv[5],argv[6]);
	  write_first_time=fopen(filename, "w");
	  sprintf(filename, "%s%stime_second2.dat", argv[5],argv[6]);
	  write_second_time=fopen(filename, "w");
	  sprintf(filename, "%s%scount2.dat", argv[5],argv[6]);
	  write_count=fopen(filename, "w");
	  sprintf(filename, "%s%scount_first2.dat", argv[5],argv[6]);
	  write_first_count=fopen(filename, "w");
	  sprintf(filename, "%s%scount_second2.dat", argv[5],argv[6]);
	  write_second_count=fopen(filename, "w");
	  sprintf(filename, "%s%sdivergence2.dat", argv[5],argv[6]);
	  write_divergence=fopen(filename, "w");
	  sprintf(filename, "%s%sdepth_white.dat", argv[5],argv[6]);
	  write_real_white=fopen(filename, "w");
	  sprintf(filename, "%s%sdepth_black.dat", argv[5],argv[6]);
	  write_real_black=fopen(filename, "w");

	}
	else{
	  sprintf(filename, "%s%stime1.dat", argv[5],argv[6]);
	  write_time=fopen(filename, "w");
	  sprintf(filename, "%s%stime_first1.dat", argv[5],argv[6]);
	  write_first_time=fopen(filename, "w");
	  sprintf(filename, "%s%stime_second1.dat", argv[5],argv[6]);
	  write_second_time=fopen(filename, "w");
	  sprintf(filename, "%s%scount1.dat", argv[5],argv[6]);
	  write_count=fopen(filename, "w");
	  sprintf(filename, "%s%scount_first1.dat", argv[5],argv[6]);
	  write_first_count=fopen(filename, "w");
	  sprintf(filename, "%s%scount_second1.dat", argv[5],argv[6]);
	  write_second_count=fopen(filename, "w");
	  sprintf(filename, "%s%sdepth_white.dat", argv[5],argv[6]);
	  write_real_white=fopen(filename, "w");
	  sprintf(filename, "%s%sdepth_black.dat", argv[5],argv[6]);
	  write_real_black=fopen(filename, "w");

	}
	fprintf(stderr, "Started.\n");

        puts(startup_message);
        signal(SIGINT, catch_sigint);

        for (i=0; i<sizeof(zobrist); i++) {
                ( (byte*)zobrist )[i] = rnd() & 0xff;
        }

        king_step[1<<0] = 1;
        king_step[1<<1] = 9;
        king_step[1<<2] = 8;
        king_step[1<<3] = 7;
        king_step[1<<4] = -1;
        king_step[1<<5] = -9;
        king_step[1<<6] = -8;
        king_step[1<<7]= -7;

        knight_jump[1<<0] = -6;
        knight_jump[1<<1] = 10;
        knight_jump[1<<2] = 17;
        knight_jump[1<<3] = 15;
        knight_jump[1<<4] = -10;
        knight_jump[1<<5] = 6;
        knight_jump[1<<6] = -15;
        knight_jump[1<<7] = -17;

        castle[A1] = CASTLE_WHITE_QUEEN;
        castle[E1] = CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN;
        castle[H1] = CASTLE_WHITE_KING;
        castle[A8] = CASTLE_BLACK_QUEEN;
        castle[E8] = CASTLE_BLACK_KING | CASTLE_BLACK_QUEEN;
        castle[H8] = CASTLE_BLACK_KING;

        cmd_new(NULL);

	/*SIMPLE I added this so we can input the random seed.*/
	if(argc>1){
	rnd_seed=atol(argv[1]);
	fprintf(stderr, "Got randome seed, it was %d\n", rnd_seed);
	}
	else{
	  rnd_seed=time(NULL);
	}

        for (;;) { /* main loop */
                if (!xboard_mode) {
                        fputs("mscp> ", stdout);
                }
                fflush(stdout);
                if (readline(line, sizeof(line), stdin) < 0) {
                        break;
                }

                if (1 != sscanf(line, "%s", name)) continue;

                for (cmd=0; mscp_commands[cmd].name != NULL; cmd++) {
                        if (0==strcmp(mscp_commands[cmd].name, name)) {
                                break;
                        }
                }
                mscp_commands[cmd].cmd(line);

                while (computer[!WTM]) {
	  /*SIMPLEmove = book_move();*/
		  move=0;
		  start=clock();
		  start_c=currentSeconds();
		  if (!move) {
		    booksize = 0;
		    memset(&core, 0, sizeof(core));
		    memset(history, 0, sizeof(history));
		    
		    if (ply%2==1 && random_countdown<=0){
		      TIME_LIMIT=1;
		      move=root_time(maxdepth, write_time, write_first_time, write_second_time, write_count, write_first_count, write_second_count);	
	      //move=p_root_time(maxdepth, write_time, write_first_time, write_second_time, write_count, write_first_count, write_second_count, write_divergence);
		      //move=p_root_search(maxdepth, write_time, write_first_time, write_second_time, write_count, write_first_count, write_second_count, write_divergence);
		      fprintf(write_real_black,"%d\n", global_depth);
		      
		    }
		      else if (random_countdown<=0){
		      //		      move = root_search(maxdepth, write_time, write_first_time, write_second_time, write_count, write_first_count, write_second_count);
			TIME_LIMIT=0.05;
		      move = root_time(maxdepth, write_time, write_first_time, write_second_time, write_count, write_first_count, write_second_count);
		      fprintf(write_real_white,"%d\n", global_depth);
		      }

		    else{
		      move = root_search(maxdepth, write_time, write_first_time, write_second_time, write_count, write_first_count, write_second_count);

		         		    }

		    fprintf(stderr,"Did move %d\n", ply);
		    random_countdown-=1;
		    if (random_countdown<= 0){
		      // maxdepth=atoi(argv[3]);
		    }//if
		    
		  }
		  
		  end=clock();
			end_c=currentSeconds();
			//	fprintf(write_time, "%f\n", ((double)end-start)/CLOCKS_PER_SEC);
			fprintf(write_time, "%f\n", end_c-start_c);
			
                        if (!move || ply >= atoi(argv[4])) {
			  printf("game over: ");
			  fprintf(write_time, "game over: ");
			  compute_attacks();
			  if (!move && enemy->attack[friend->king] != 0) {
			    puts(WTM ? "0-1" : "1-0");
			    fputs(WTM ? "0-1\n" : "1-0\n",write_time);
			  } else {
			    puts("1/2-1/2");
			    fputs("1/2-1/2\n",write_time);
			  }
                                computer[0] = computer[1] = 0;
				
                                break;
                        }
                        printf("%d. ... ", 1+ply/2);
                        print_move_long(move);
                        putc('\n', stdout);
			
                        make_move(move);
                        hash_stack[ply] = compute_hash();

                        print_board();
                }
        }
	fclose(write_time);
fclose(write_first_time);
fclose(write_second_time);
fclose(write_count);
fclose(write_first_count);
fclose(write_second_count);
fclose(write_real_white);
fclose(write_real_black);
 if(atoi(argv[2])==1)
   fclose(write_divergence);
        return 0;
}

/*----------------------------------------------------------------------+
 |                                                                      |
 +----------------------------------------------------------------------*/

