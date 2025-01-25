// TAPjack source code
// last modified 4/19/2024
// ASCII cards designed by Kevin Lei
// Ultrasonic Sensor functions designed by Quinn Frady
// game logic and screen display functions designed by Nathan Ramos

// for USART
#define F_CPU 8000000UL
#define BAUD 38400
#define MYUBRR F_CPU/16/BAUD-1
#define ASCII_NUM 48
// for USS
#define HCSR04CONST 58.2
#define THRESHOLD 200
#define WIDTH 22.0
#define DIST_HIT 8.0
#define DIST_STAY 35.0
// for ADC
#define VREF 5
#define STEPS 1024
#define STEPSIZE VREF/STEPS
// blackjack constants
#define NUMPLAYERS 5
#define SINGLEDECK 52
#define MAXSUIT 13
#define MAXRANK 4
#define MAXHAND 12
//display config
// For 150% Display scale: 37 char terminal height, hide task bar
// For 175% display scaling: 28x165, hide task bar
#define TERMHEIGHT 28
#define TERMWIDTH 165
// delays
#define DELAY_INPUT 200
#define DELAY_REFRESH 2000
#define DELAY_READ 4000
#define DELAY_RESULTS 10000

// DO NOT CHANGE
#define NL '\n'
#define HIT 1
#define STAY 2
#define NOACTION 3
#define ERROR 0
#define DEALER 0
#define P1 1
#define P2 2
#define P3 3
#define P4 4

#include <avr/io.h>
#include <avr/delay.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <string.h>

typedef struct hand {
	int rank[MAXHAND]; // 1 = Ace, 2-9, 10 = T, 11 = Jack, 12 = Queen, 13 = King
	char suit[MAXHAND]; // h = hearts, d = diamonds, c = clubs, s = spades
	int isFaceDown[MAXHAND]; // true = card value is hidden, false = card value is shown
	int handsize; // number of cards in one player's hand
	int handvalue; // how many points the player's hand is worth
	int busted; // true = hand value over 21, false = hand value 21 or under
	int soft; // true = hand value can decrease to stay under 22 (has Aces), false = hand value cannot decrease
    int empty; // true = hand has zero cards, false = hand has at least one card
	} hand;

// global vars
char suitG[SINGLEDECK]; // h = hearts, d = diamonds, c = clubs, s = spades
int rankG[SINGLEDECK]; // 1 = Ace, 2-9, 10 = T, 11 = Jack, 12 = Queen, 13 = King, 14 = blind
int indexG; // index of unassigned card in deck

int outcome[NUMPLAYERS]; // win = 1, loss = 0, push = 2
int SCREENFILL = TERMHEIGHT; // for screen refresh
int timerOverflow = 0; // for ultrasonic sensor

hand p1a, p1b, p2a, p2b, p3a, p3b, p4a, p4b; // 4 players each have a main hand and an extra hand for a split scenario
hand dealer; // dealer's hand

// display logic
void cardPrint(hand *p); // print entire hand
char rankConvert(int rank);
void fillScreen(int lines); // fill the rest of the terminal
void alignCenter(int strWidth); // displays a line of strings that is centered
void dispUpper(int ID); // prints everything from the dealer's hand upwards
void dispIntro(); // display welcome greeting & credits
void dispBlank(); // display blank screen
void dispRound(int round); // display round screen
void dispTurn(int ID); // display turn screen
void dispResults(); // decides win or push or loss

// reset logic
void emptyHand(hand *p); // empty player's hand 
void newRound(); // starts a brand new round of blackjack
void initDeck(); // initialize deck of 52 playing cards
void shuffleDeck(); // shuffle the deck
int ADC_rand(); // grab random digital voltage from ADC (light sensor)

// game logic
void playTurn(int ID); // enact player's turn
void dealCard(hand *p); // deal 1 card
void split(int ID);
int userInput(); // get player's desired input
void selectPlayer(int ID, hand** pA, hand** pB); // get desired player hands

// USART
void USART_init(unsigned int ubrr); // init USART
void send(const char* data); // send string
void sendChar(const char data); // send char

// UltraSonicSensor
void USS_init(); // init USS
double USS_distance(); // get distance
int USS_move(); // get move from the sensor

// interrupt subroutine for handling the timer
ISR(TIMER1_OVF_vect)
{
	cli(); // disable interrupts
	timerOverflow++;	// Increment Timer Overflow count
	sei(); // enable interrupts
}

int main() {
    // initialize USART
	USART_init(MYUBRR);
	// initialize USS
	USS_init();
	// initialize deck of cards
	initDeck();
    // seed rand()
    srand(ADC_rand());

    dispBlank(); // display blank screen
    dispIntro(); // display introduction screen
    dispBlank(); // display blank screen

    // game runs for 999 rounds
    for (int round = 1; round < 1000; round++) {
        dispRound(round); // display round screen
        newRound(); // starts a new round

        // deal out the starting hands in proper casino order
		for (int i = 0; i < 2; i++) {
			dealCard(&p1a);
			dealCard(&p2a);
			dealCard(&p3a);
			dealCard(&p4a);
			dealCard(&dealer);
		}
		dealer.isFaceDown[0] = 1; // hide dealer's first card from view
        
        // Give each player a turn to interact with the game
        for(int pNow = P1; pNow <= P4; pNow++) {
            dispBlank(); // display blank screen
			dispTurn(pNow); // display player turn screen
            playTurn(pNow); // allow player input for their turn
		}
		dealer.isFaceDown[0] = 0; // show dealer's first card
        // loop for dealer's turn
        while (1) {
            // display dealer's cards for one screen instance
            SCREENFILL = TERMHEIGHT;
            dispUpper(DEALER);
            fillScreen(SCREENFILL);
			_delay_ms(DELAY_REFRESH);
            sendChar(NL);

            // display dealer's cards for another screen instance
            SCREENFILL = TERMHEIGHT;
            dispUpper(DEALER);
            sendChar(NL);
            sendChar(NL);
            SCREENFILL -= 2;
            if (dealer.busted) { // dealer busts
                alignCenter(14);
                send("Dealer BUSTED!");
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_READ);
                sendChar(NL);
                break;
            } else if ((dealer.handvalue < 17) || (dealer.handvalue == 17 && dealer.soft)) { // dealer can continue drawing
                alignCenter(12);
				send("Dealer hits!");
				dealCard(&dealer);
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_REFRESH);
                sendChar(NL);
			} else { // dealer has reached a hand value from hard 17 up to 21
                alignCenter(20);
				send("Dealer stays with ");
				char d[3];
				itoa(dealer.handvalue,d,10);
				send(d);
				send("!");
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_READ);
                sendChar(NL);
				break;
			}
        }
        dispResults(); // display results screen
    }
}
// displays entire hand of given player
void cardPrint(hand *p) {
    int numCards = p->handsize;
	unsigned int k;
	/*Border(s)*/
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
	 send(" +-----------+");
	}
	sendChar(NL);
	/*TopRank(s)*/
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
	 if (p->isFaceDown[k]) { send(" |###########|"); }
	 else { 
		 send(" | ");
		 sendChar(rankConvert(p->rank[k])); 
		 send("         ");
		 send("|");
		 }
	}
	sendChar(NL);
	/*Spacer(s)*/
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
	 if (p->isFaceDown[k]) { send(" |####   ####|"); }
	 else { send(" |           |"); }
	}
	sendChar(NL);

	// Printing 1st line of body
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
        if (p->isFaceDown[k]) { 
            send(" |#### U ####|");
        }
        else switch(p->suit[k]) {
            case 'h': send(" |    _ _    |"); break;
            case 'd': send(" |     ^     |"); break;
            case 'c': send(" |     _     |"); break;
            case 's': send(" |     .     |"); break;
            default : send(" |   ERROR   |"); break;
        }
	}
	sendChar(NL); // Next line
	// Printing 2nd line of body
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
        if (p->isFaceDown[k]) {
            send(" |#### N ####|");
        }
        else switch(p->suit[k]) {
            case 'h': send(" |   ( V )   |"); break;
            case 'd': send(" |    / \\    |"); break;
            case 'c': send(" |    (&)    |"); break;
            case 's': send(" |    /&\\    |"); break;
            default : send(" |   ERROR   |"); break;
        }
	}
	sendChar(NL); // Next line
	// Printing 3rd line of body
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
        if (p->isFaceDown[k]) {
            send(" |#### L ####|");
        }
        else switch(p->suit[k]) {
            case 'h': send(" |    \\ /    |"); break;
            case 'd': send(" |    \\ /    |"); break;
            case 'c': send(" |   (&&&)   |"); break;
            case 's': send(" |   (&&&)   |"); break;
            default : send(" |   ERROR   |"); break;
        }
	}
	sendChar(NL); // Next line
	 // Printing 4th line of body
    alignCenter(numCards*14);
	 for(k = 0; k < numCards; k++) {
            if (p->isFaceDown[k]) {
                send(" |#### V ####|");
            }
            else switch(p->suit[k]) {
                case 'h': send(" |     V     |"); break;
                case 'd': send(" |     V     |"); break;
                case 'c': send(" |     ^     |"); break;
                case 's': send(" |     ^     |"); break;
                default : send(" |   ERROR   |"); break;
            }
	 }
	sendChar(NL); // Next line
    /*Spacer(s)*/
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
	 if (p->isFaceDown[k]) { send(" |####   ####|"); }
	 else { send(" |           |"); }
	}
	sendChar(NL);
	/*BotRank(s)*/
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
	 if (p->isFaceDown[k]) { send(" |###########|"); }
	 else {
		 send(" |");
		 send("         ");
		 sendChar(rankConvert(p->rank[k]));
		 send(" |");
	 }
	}
	sendChar(NL);
	/*Border(s)*/
    alignCenter(numCards*14);
	for(k = 0; k < numCards; k++) {
	 send(" +-----------+");
	}
	sendChar(NL);
	SCREENFILL -= 10;
}
// Converts numeric card to it's respective char representation
char rankConvert(int rank) {
	switch (rank) {
		case  1: return 'A';
		case 13: return 'K';
		case 12: return 'Q';
		case 11: return 'J';
		case 10: return 'T';
		default: return (rank + ASCII_NUM);
	}
}
// fills rest of the display screen with blank space
void fillScreen(int lines) {
    for (;lines > 0; lines--) {
        sendChar(NL);
    }
}
// center-justifies text on the screen
void alignCenter(int strWidth) {
    for (int i = (TERMWIDTH - strWidth)/2; i > 0; i--) {
        sendChar(' ');
    }
}
// displays the upper part of the screen right before cards are displayed
void dispUpper(int ID) {
    char temp[6];
    hand *pA, *pB;
	if (ID == DEALER) {
		send("DEALER'S TURN.  ");
        ID = 4;
	} else {
		send("PLAYER ");
		sendChar(ID + ASCII_NUM);
		send("'S TURN.");
    }
    send("				");

    for (int i = 1; i <= ID; i++) {
        selectPlayer(i, &pA, &pB);
        send("Player ");
		sendChar(i + ASCII_NUM);
		send("'s hand: [");
        itoa(pA->handvalue,temp,10);
        send(temp);
        
        if (!pB->empty) {
            send("][");
            itoa(pB->handvalue,temp,10);
            send(temp);
        }
        send("]	");
    }
    sendChar(NL);
    sendChar(NL);
    alignCenter(18);
    send("Dealer is showing:");
    sendChar(NL);
    SCREENFILL -= 3;

    cardPrint(&dealer);
}
// displays introduction screen
void dispIntro() {
    SCREENFILL = TERMHEIGHT;
    fillScreen(2);
    SCREENFILL -= 2;
    alignCenter(45); send("WELCOME TO TOUCHLESS AUTOMATED PLAY BLACKJACK\n");
    alignCenter(3); send("AKA\n");
    alignCenter(67); send(" ______   ______     ______     __     ______     ______     __  __\n");   
    alignCenter(67); send("/\\__  _\\ /\\  __ \\   /\\  == \\   /\\ \\   /\\  __ \\   /\\  ___\\   /\\ \\/ /\n");   
    alignCenter(67); send("\\/_/\\ \\/ \\ \\  __ \\  \\ \\  _-/  _\\_\\ \\  \\ \\  __ \\  \\ \\ \\____  \\ \\  _\"-.\n"); 
    alignCenter(67); send("   \\ \\_\\  \\ \\_\\ \\_\\  \\ \\_\\   /\\_____\\  \\ \\_\\ \\_\\  \\ \\_____\\  \\ \\_\\ \\_\\\n");
    alignCenter(67); send("    \\/_/   \\/_/\\/_/   \\/_/   \\/_____/   \\/_/\\/_/   \\/_____/   \\/_/\\/_/\n");
	SCREENFILL -= 10;
	fillScreen(SCREENFILL);
	_delay_ms(DELAY_REFRESH);
    sendChar(NL);
	
	SCREENFILL = TERMHEIGHT;
	fillScreen(13);
	alignCenter(10);
	send("CREATED BY\n");
	alignCenter(38);
	send("Nathan Ramos, Kevin Lei, & Quinn Frady");
	SCREENFILL -= 16;
	fillScreen(SCREENFILL);
	_delay_ms(DELAY_REFRESH);
    sendChar(NL);
}
// displays blank screen
void dispBlank() {
    // blank screen
	SCREENFILL = TERMHEIGHT;
	fillScreen(SCREENFILL);
	_delay_ms(DELAY_REFRESH);
    sendChar(NL);
}
// displays round screen
void dispRound(int round) {
    SCREENFILL = TERMHEIGHT;
    fillScreen(15);
    alignCenter(7);
    send("Round ");
    char str[5];
    itoa(round,str,10);
    send(str);
    SCREENFILL -= 15;
    fillScreen(SCREENFILL);
    _delay_ms(DELAY_REFRESH);
    sendChar(NL); 
}
// displays player turn screen
void dispTurn(int ID) {
    SCREENFILL = TERMHEIGHT;
    fillScreen(15);
    if (ID == DEALER) {
        alignCenter(13);
        send ("DEALER'S TURN");
    } else {
        alignCenter(15);
        send("PLAYER ");
        sendChar(ID + ASCII_NUM);
        send("'S TURN");
    }
    SCREENFILL -= 15;
    fillScreen(SCREENFILL);
    _delay_ms(DELAY_REFRESH);  
    sendChar(NL);
}
// displays results of the round
void dispResults() {
    SCREENFILL = TERMHEIGHT;
    dealer.isFaceDown[0] = 0;
    dispUpper(DEALER);
    sendChar(NL);
    if (dealer.busted) {
        alignCenter(14);
        send("Dealer BUSTED!");
    } else {
        alignCenter(20);
        send("Dealer stays with ");
        char d[3];
		itoa(dealer.handvalue,d,10);
        send(d);
    }
    sendChar(NL);
    sendChar(NL);
    sendChar(NL);
    SCREENFILL -= 4;

    hand *pA, *pB;
    for (int ID = P1; ID <= P4; ID++) {
        selectPlayer (ID, &pA, &pB);
        send("      Player ");
        sendChar(ID + ASCII_NUM);
        if (pA->busted) {
            send(" LOST with ");
        } else if (pA->handvalue == dealer.handvalue) {
            send(" PUSHED with ");
        } else if (pA->handvalue < dealer.handvalue && !dealer.busted) {
            send(" LOST with ");
        } else if (pA->handvalue > dealer.handvalue || dealer.busted) {
            send(" WON with ");
        } else {
            send("ERROR in dispResults()");
        }
        char temp[3];
		itoa(pA->handvalue,temp,10);
		send(temp);
        if (!pB->empty) {
            send (" and");
            if (pB->busted) {
                send(" LOST with ");
            } else if (pB->handvalue == dealer.handvalue) {
                send(" PUSHED with ");
            } else if (pB->handvalue < dealer.handvalue && !dealer.busted) {
                send(" LOST with ");
            } else if (pB->handvalue > dealer.handvalue || dealer.busted) {
                send(" WON with ");
            } else {
                send("ERROR in dispResults()");
            }
            char temp[3];
		    itoa(pB->handvalue,temp,10);
		    send(temp);
        }
        sendChar(NL);
        sendChar(NL);
        SCREENFILL -= 2;
    }
    fillScreen(SCREENFILL);
    _delay_ms(DELAY_RESULTS);
	sendChar(NL);
	return;
}
// empties given hand
void emptyHand(hand *p) {
    for (int i = 0; i < MAXHAND; i++) {
		p->rank[i] = 0;
		p->suit[i] = 0;
		p->isFaceDown[i] = 0;
	}
	p->handsize = 0;
	p->handvalue = 0;
	p->busted = 0;
	p->soft = 0;
    p->empty = 1;
}
// initializes everybody's hands and shuffles the deck
void newRound() {
    emptyHand(&dealer);
	emptyHand(&p1a);
	emptyHand(&p2a);
	emptyHand(&p3a);
	emptyHand(&p4a);
    emptyHand(&p1b);
	emptyHand(&p2b);
	emptyHand(&p3b);
	emptyHand(&p4b);
	shuffleDeck();
}
// initializes a standard 52 card poker deck
void initDeck() {
    indexG = 0;
	for (int i = 0; i < SINGLEDECK; i++) {
		if		(i > (3 * MAXSUIT - 1)) { suitG[i] = 'h'; }
		else if (i > (2 * MAXSUIT - 1)) { suitG[i] = 'd'; }
		else if (i > (MAXSUIT - 1))		{ suitG[i] = 'c'; }
		else							{ suitG[i] = 's'; }
		rankG[i] = (i % MAXSUIT) + 1;
	}
	// EXTREME TAPJACK: ACE OF SPADES
// 	for (int i = 0; i < SINGLEDECK; i++) {
// 		rankG[i] = 1;
// 		suitG[i] = 's';
// 	}
}
// shuffles a deck of playing cards using rand() seeded through lavaRND
void shuffleDeck() {
    indexG = 0;
	for (int i = 0; i < SINGLEDECK; i++) {
		unsigned int j = rand() % SINGLEDECK;
		unsigned int tempR = rankG[j];
		unsigned char tempS = suitG[j];
		rankG[j] = rankG[i];
		suitG[j] = suitG[i];
		rankG[i] = tempR;
		suitG[i] = tempS;
	}
}
// reads light values from light sensor and converts analog value to digital for RNG seed
int ADC_rand() {
    DDRC = (0 << PINC0); // ADC0 input
	ADMUX = (1 << REFS0) | (0 << MUX0); // input ADC0, left justified, AVcc
	// enable ADC, set prescaler to 64
	ADCSRA |= (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1);
	ADCSRA |= (1 << ADSC); // start ADC conversion
	while (ADCSRA & (1 << ADSC)); // wait until ADC finishes
	ADCSRA ^= (1 << ADEN); // disable ADC
	return ADC;
}
// display's player's active turn on screen and accepts player input
void playTurn(int ID) {
    hand *pA, *pB;
	int askSplit = 1;
    selectPlayer(ID, &pA, &pB);
    while (1) { // player's turn for their first hand
		// display player's turn screen
        SCREENFILL = TERMHEIGHT;
		dispUpper(ID);
		sendChar(NL);
        alignCenter(23);
		send("Your current hand: [");
		char temp[3];
		itoa(pA->handvalue,temp,10);
		send(temp);
		send("]");
		sendChar(NL);
		cardPrint(&(*pA));
		SCREENFILL -= 2;
		fillScreen(SCREENFILL);
		_delay_ms(DELAY_REFRESH);
		sendChar(NL);
		
        // display's player's turn screen along with interactive questions
        SCREENFILL = TERMHEIGHT;
        dispUpper(ID);
        sendChar(NL);
        alignCenter(23);
        send("Your current hand: [");
        itoa(pA->handvalue,temp,10);
        send(temp);
        send("]");
        sendChar(NL);
        cardPrint(&(*pA));
        SCREENFILL -= 2;
        sendChar(NL);
        sendChar(NL);
        SCREENFILL -= 2;
        if ((pA->rank[0] == pA->rank[1]) && askSplit) { // player's hand has the option to split
            alignCenter(34);
            send("SPLIT? (HIT for YES) (STAY for NO)");
            fillScreen(SCREENFILL);
            if (userInput() == HIT) {
                sendChar(NL);
                split(ID); // handles the screen display and refresh
            }
			askSplit = 0; 
            continue;
        } else if (pA->busted) { // player's hand is worth more than 21
            alignCenter(11);
            send("You BUSTED!");
            fillScreen(SCREENFILL);
            _delay_ms(DELAY_READ);
            sendChar(NL);
            break;
        } else if (pA->handvalue == 21) { // player's hand is worth 21
            alignCenter(16);
            send("You got TAPJACK!");
            fillScreen(SCREENFILL);
            _delay_ms(DELAY_READ);
            sendChar(NL);
            break;
        } else { // player can hit or stay
            alignCenter(12);
            send("HIT or STAY?");
            fillScreen(SCREENFILL);
            int choice = userInput();
            _delay_ms(DELAY_INPUT);
            sendChar(NL);

            SCREENFILL = TERMHEIGHT;
            dispUpper(ID);
            sendChar(NL);
            alignCenter(23);
            send("Your current hand: [");
            char temp[3];
            itoa(pA->handvalue,temp,10);
            send(temp);
            send("]");
            sendChar(NL);
            cardPrint(&(*pA));
            SCREENFILL -= 2;
            sendChar(NL);
            sendChar(NL);
            SCREENFILL -= 2;
            if (choice == HIT) { // player decided to hit
                alignCenter(8);
                send("You hit!");
                dealCard(&(*pA));
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_REFRESH);
                sendChar(NL);
            } else if (choice == STAY) { // player decided to stay
                alignCenter(11);
                send("You stayed!");
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_READ);
                sendChar(NL);
                break;
            } else { // unintended behavior
                send("ERROR in playTurn()");
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_REFRESH);
                sendChar(NL);
                break;
            }
        }
        
    }
    if (pB->empty) { // player did not split their hand an only has one hand
        return;
    }
    while (1) { // player's turn for their second hand
        SCREENFILL = TERMHEIGHT;
        dispUpper(ID);
        sendChar(NL);
        alignCenter(23);
        send("Your current hand: [");
        char temp[3];
        itoa(pB->handvalue,temp,10);
        send(temp);
        send("]");
        sendChar(NL);
        cardPrint(&(*pB));
        SCREENFILL -= 2;
        sendChar(NL);
        sendChar(NL);
        SCREENFILL -= 2;
        if (pB->busted) {
            alignCenter(11);
            send("You BUSTED!");
            fillScreen(SCREENFILL);
            _delay_ms(DELAY_REFRESH);
            sendChar(NL);
            break;
        } else if (pB->handvalue == 21) {
            alignCenter(16);
            send("You got TAPJACK!");
            fillScreen(SCREENFILL);
            _delay_ms(DELAY_REFRESH + 2000);
            sendChar(NL);
            break;
        } else {
            alignCenter(12);
            send("HIT or STAY?");
            fillScreen(SCREENFILL);
            int choice = userInput();
            _delay_ms(DELAY_INPUT);
            sendChar(NL);

            SCREENFILL = TERMHEIGHT;
            dispUpper(ID);
            sendChar(NL);
            alignCenter(23);
            send("Your current hand: [");
            char temp[3];
            itoa(pB->handvalue,temp,10);
            send(temp);
            send("]");
            sendChar(NL);
            cardPrint(&(*pB));
            SCREENFILL -= 2;
            sendChar(NL);
            sendChar(NL);
            SCREENFILL -= 2;
            if (choice == HIT) {
                alignCenter(8);
                send("You hit!");
                dealCard(&(*pB));
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_REFRESH);
                sendChar(NL);
            } else if (choice == STAY) {
                alignCenter(11);
                send("You stayed!");
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_REFRESH);
                sendChar(NL);
                break;
            } else {
                send("ERROR in playTurn()");
                fillScreen(SCREENFILL);
                _delay_ms(DELAY_REFRESH);
                sendChar(NL);
                break;
            }
        }
        
    }
}
// deal one available card from the deck to the given player
// also updates their handsize, handvalue, and other hand characteristics
void dealCard(hand *p) {
    if ((indexG >= SINGLEDECK) || (p->handsize >= MAXHAND)) {
		send("ERROR: Cannot Deal Card!\n");
		return;
	}
	int rank = rankG[indexG];
	p->rank[p->handsize] = rank;
	p->suit[p->handsize] = suitG[indexG];
	p->handsize++;
	indexG++;
	switch (rank) {
		case 1:
		p->handvalue += 11;
		p->soft++;
		break;
		case 10:
		case 11:
		case 12:
		case 13:
		p->handvalue += 10;
		break;
		default:
		p->handvalue += rank;
	}
	if (p->handvalue > 21) {
		if (p->soft > 0) {
			p->handvalue -= 10;
			p->soft--;
		}
		else { p->busted = 1; }
	}
    p->empty = 0;
} 
// splits given player's hand 
void split(int ID) {
    hand *pA, *pB;
    selectPlayer(ID, &pA, &pB);
    SCREENFILL = TERMHEIGHT;
    dispUpper(ID);
    sendChar(NL);
    alignCenter(23);
    send("Your current hand: [");
    char temp[3];
    itoa(pA->handvalue,temp,10);
    send(temp);
    send("]");
    sendChar(NL);
    cardPrint(&(*pA));
    SCREENFILL -= 2;
    sendChar(NL);
    sendChar(NL);
    SCREENFILL -= 2;
    alignCenter(10);
    send("You split!");
    fillScreen(SCREENFILL);
    int cardR = pA->rank[1];
    char cardS = pA->suit[1];
    pB->rank[0] = cardR;
    pB->suit[0] = cardS;
    pB->handsize++;
    pB->handvalue += cardR;
    pA->handsize--;
    pA->handvalue -= cardR;
    if (cardR == 1) {
        pB->soft = 1;
        pB->handvalue = 11;
        pA->soft = 1;
        pA->handvalue = 11;
    }
    dealCard(&(*pA));
    dealCard(&(*pB));
    _delay_ms(DELAY_INPUT);
    sendChar(NL);
}
// wait for user input
int userInput() {
    while (USS_move() != NOACTION); // wait for USS input area to be cleared
	while (1) {
		/*_delay_ms(DELAY_INPUT);*/
		switch (USS_move()) {
			case STAY: return STAY;
			case HIT: return HIT;
			case NOACTION: break;
		}
	}
}
// retrieves given player's hands for modification or display
// given player *ID*, place their first hand in *pA* and their second hand in *pB*
void selectPlayer(int ID, hand** pA, hand** pB) {
    switch (ID) {
        case P1:
            *pA = &p1a;
            *pB = &p1b;
            break;
        case P2:
            *pA = &p2a;
            *pB = &p2b;
            break;
        case P3:
            *pA = &p3a;
            *pB = &p3b;
            break;
        case P4:
            *pA = &p4a;
            *pB = &p4b;
            break;
        case DEALER:
            send("ERROR in selectPlayer()\n");
            break;
    }
}
// initialize USART protocol for communication between atmega and atmel terminal
void USART_init(unsigned int ubrr) {
    //Set baud rate
	UBRR0H = (unsigned char)(ubrr>>8);
	UBRR0L = (unsigned char) ubrr;
	// enable transmitter
	UCSR0B = (1<<TXEN0);
	// Set frame format: async, no parity, 1 stop bit, , 8 data bits
	UCSR0C = (0<<UMSEL01)|(0<<UMSEL00)|(0<<UPM01)|(0<<UPM00)|(0<<USBS0)|(1<<UCSZ01)|(1<<UCSZ00);
}
// transmit string to USART channel
void send(const char* data) {
    while (*data) {
		//check if buffer is empty so that data can be written to transmit
		while (!(UCSR0A & (1 << UDRE0)));
		UDR0 = *data; //copy data to be sent to UDR0
		++data;
	}
}
// transmits single character to USART channel
void sendChar(const char data) {
    //check if buffer is empty so that data can be written to transmit
	while (!(UCSR0A & (1 << UDRE0)));
	UDR0 = data; //copy character to be sent to UDR0
}
// initialize ultrasonic sensor for touchless controls
void USS_init() {
    /*Ultrasonic Initialization
	PB0 is the Echo Pin & PB1 is the Trigger*/
	
	//Define Pins
	#define Trigger_pin PB1	//This is the UltraSonic Sensors Trigger Pin
	
	//GPIO Programming
	DDRB = 0x02;	//Output for Ultrasonic Trigger Pin
	
	//Timer 1 Initialization
	TIMSK1 = (1 << TOIE1);	//Enable Timer1 overflow interrupts
	TCCR1A = 0;				//Set all bit to zero Normal operation
	
	//Enable Global Interrupts
	sei();
}
// calculate ultrasonic sensor distance to determine which control was selected
double USS_distance() {
    TCCR1B |= (1 << CS10);  //Prescaler, Start Timer
	
	/*Declare variables*/
	long count;				//var to store the received input from ultrasonic
	double distance;	//var to store the received distance from the USART
	
	/*Receive the UltraSonic sensors values*/
	PORTB |= (1 << Trigger_pin);			//Begin Trigger
	_delay_us(10);
	PORTB &= (~(1 << Trigger_pin));			//Cease Trigger
	TCNT1 = 0;								//Clear Timer counter
	TCCR1B = (1 << ICES1) | (1 << CS10);	//Capture rising edge, prescaler 1
	TIFR1 = (1 << ICF1) | (1 << TOV1);		// Clear ICP flag & Clear Timer Overflow flag
	
	/*Calculate width of Echo by Timer 1 ICP*/
	while ((TIFR1 & (1 << ICF1)) == 0);		// Wait for rising edge
	TCNT1 = 0;								// Clear Timer counter
	TCCR1B = (1 << CS10);					// Capture falling edge
	TIFR1 = (1 << ICF1) | (1 << TOV1);		// Clear ICP flag & Clear Timer Overflow flag
	timerOverflow = 0;						// Clear Timer overflow count
	while ((TIFR1 & (1 << ICF1)) == 0);		//Wait for falling edge
	count = ICR1 + (65535 * timerOverflow);	//Take value of capture register and calculate width
	distance = (double) count / (HCSR04CONST*F_CPU/1000000);		//Calculate Distance
	
	TCCR1B ^= (1 << CS10);  //No prescaler, Stop Timer
	return distance;
}
// determine player's move (hit, stay) from ultrasonic sensor distance calculated
int USS_move() {
    int countS = 0;
    int countH = 0;
    int countN = 0;
    double distance = USS_distance();
    while(1) 
    {
        /*Determine what the chosen move was*/
        if (distance > DIST_HIT && distance < (DIST_HIT + WIDTH)) 
        {		//test for hit
            countS = countN = 0;	//reset the other move counters
            countH++;					//increment this moves counter
            if (countH > THRESHOLD) 
            {			
                return HIT;					//HIT
            }
        }
        else if (distance > DIST_STAY && distance < (DIST_STAY + WIDTH)) 
        {	//test for stay
            countH = countN = 0;	//reset the other move counters
            countS++;					//increment this moves counter
            if (countS > THRESHOLD) 
            {		
                return STAY;					//STAY
            }
        }
        else 
        {										//if not hit or stay, OR if nothings happening
            countH = countS = 0;	//reset the other move counters
            countN++;					//increment this moves counter
            if (countN > THRESHOLD) 
            {		
                return NOACTION;					//NOACTION
            }
        }
        _delay_ms(1);
    }
}
