/*
Course : UVic Mechatronics 458
Milestone : Final Project
Title : Object Sorting Apparatus

Name 1: Devon Bowen, Student ID: V00901318
Name 2: Jack Mazzei, Student ID: V00890412

This program utilizes internal and external interrupts to identify and sort objects of different material and colour. It consists of a main
loop which primarily has stepper motor control and DC motor control. Outside this, interrupt vectors contain flags which indicate that specific
conditions have been met allowing corresponding commands to be executed. It also uses functions such as ADC, internal timers, and a linkedlist.
*/

//Standard Library
#include <stdlib.h>			
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>
#include "lcd.h"
#include "LinkedQueue.h"


#define counterClockwise 0b00000111; //Counter clockwise rotation for DC motor declaration
#define stop 0b00001111; //DC motor stop declaration


//Stepper Motor Definitions in full step 2 phase mode
#define Step1 0b00110110;
#define Step2 0b00101110;
#define Step3 0b00101101;
#define Step4 0b00110101;


//Steps to corresponding position on sorting tray, black being home. Note a rotation of 50 steps corresponds to approximately 90 degrees.
#define blackPosition 0;
#define whitePosition 100;
#define stPosition 50;
#define alPosition 150;


//Global Variable Declarations
//Counters used when displaying sorted quantities
volatile unsigned int blCounter;
volatile unsigned int whCounter;
volatile unsigned int stCounter;
volatile unsigned int alCounter;


//Parameters for position and direction movements for the stepper motor
volatile int currentPosition; //Track step position
volatile int stepperStagePosition; //Track at what point the stepper is at (i.e. out of the 4 firing cycles)
volatile int desiredPosition; //Track desired position
volatile int minSteps; //Calculates the minimum amount of steps between the current position and desired position
volatile int directionCW = 1; //Define CCW for stepper motor
volatile unsigned int stepsLeft; //Used if desired position is current position to allow for case to occur
volatile unsigned int stepperStageFlag; //Flag to start stepper rotation


//Parameters for time, velocity and acceleration profiles for the stepper motor
volatile unsigned int velt = 20500; // Maximum time passed between coil firings for stepper motor
volatile unsigned int timeVel; // Tracks the time between coil firings. Is updated varaible "velt"
//Use of an array prevents calculation when time is critical
int accelArray[] = {20000, 19900, 19650, 19150, 18150, 16400, 13650, 10650, 7900, 6400}; // Numbers where found by graphing in excel


//Interrupt Flags
volatile unsigned int INT2_result_flag;
volatile unsigned int INT3_result_flag;
volatile unsigned int ADC_result_flag;
volatile unsigned int crankTimer_result_flag; //Used for microsecond timer "crankTimer"


//Related to Pause and LCD display
volatile unsigned int pauseButton = 1;
volatile unsigned int listSize;
volatile unsigned int mat; //Used to categorize material type. Assigned int to specific colour


//Ramp down function
volatile unsigned int eStop; //Indicates the ramp down button was pushed
volatile unsigned int killProgram; //Counts remaining objects needed to sort before stopping


//ADC
volatile unsigned int ADC_result;
volatile unsigned int minVal; // Stores the minimum ADC result from a set of readings


//Linked list Declarations
link *head;	/* The ptr to the head of the queue */
link *tail;	/* The ptr to the tail of the queue */
link *newLink; /* A ptr to a link aggregate data type (struct) */
link *rtnLink; /* same as the above */
element eTest; /* A variable to hold the aggregate data type known as element */


//This is where the majority of the program runs. Functions and interrupts exist outside, but aside from this 
void main(){

	CLKPR = 0x80; //Pre scaling clock
	CLKPR = 0x01; //Sets clock to 8mhz
	DDRC = 0xff; //Port C as output
	DDRB = 0xff; //Port B as output
	DDRA = 0xff; //Port A as output
	DDRL = 0x70; //Port L as output

	cli(); //Disable all interrupts

	//Config the external interrupt
	EICRA |= _BV(ISC01); // Falling edge int 0 
	EICRA |= _BV(ISC11) | _BV(ISC10) ; // Rising edge int 1	
	EICRA |= _BV(ISC21); // Falling edge int 2
	EICRA |= _BV(ISC31) | _BV(ISC30); //Rising edge int 3
	EIMSK |= 0x0F; //Enables INT3 and INT2
	
	//Config ADC
	ADCSRA |= _BV(ADEN); //Enable ADC this is required before voltage reference and input chanel is selected
	ADCSRA |= _BV(ADIE); //Enable interrupt of ADC
	ADMUX |= _BV(REFS0); //ADC generates a 10bit result.

	sei(); //Sets the Global Enable for all interrupts
	
	//Points links for LinkedList to Null
	rtnLink = NULL;
	newLink = NULL;
	
	InitLCD(LS_BLINK|LS_ULINE); //Initialize LCD module
	PWMgen(); //Initializes PWM
	LCDClear(); //Clear the screen
	PORTB = counterClockwise; //Starts DC Motor
	setup(&head, &tail); //Initializes LinkedList
		
			
	//Stepper Motor home position initialization
	while((PINL&0b10000000) == 0b10000000){  //PINL for hall effect sensor. While the hall effect sensor is active high, it is not in the correct position
		//if statement is needed for each step to energize coils in correct order
		if((PINL&0b10000000) == 0b10000000)
			PORTA = Step1;
			mTimer(20); //Pause between coil firings
		if((PINL&0b10000000) == 0b10000000)
			PORTA = Step2;
			mTimer(20);
		if((PINL&0b10000000) == 0b10000000)
			PORTA = Step3;
			mTimer(20);
		if((PINL&0b10000000) == 0b10000000)
			PORTA = Step4;
			mTimer(20);
	} //while


	//Closed loop, this is where the program runs after the initialization
	while (1){	
		
		//Sets conditions which results in no motor movement if the desired position is the current position
		if(stepsLeft == 1){
			mTimer(50); // Time selected to prevent rim contact
			
			//Sets flags to true to allow for entry into the categorization code blocks in if () below
			INT2_result_flag = 1;
			stepperStageFlag = 1;
			stepsLeft = 0;
		} //if
		
		//The following loop is for the bucket stage. Once INT2 flag has been set to true, that means there is an object at the end of the belt waiting to be sorted. The following code highlights 
		//how the material is deQueued and the corresponding bucket position is found
		if((stepperStageFlag == 1) && (stepsLeft == 0)){ 
			dequeue(&head, &tail, &rtnLink); //Dequeues the first element in the list
			
			//Material categorization. Integers assigned to specific colors. Once deQueued, add to the count of sorted items for that specific material
			if(rtnLink->e.itemCode == 5){ //mat = 5 is black
				desiredPosition =  blackPosition; //This "desiredPosition" is used for the stepper motor
				blCounter++;
			}else if(rtnLink->e.itemCode == 6){ //mat = 6 is white
				desiredPosition =  whitePosition;
				whCounter++;
			}else if(rtnLink->e.itemCode == 7){ //mat = 7 is steel
				desiredPosition =  stPosition;
				stCounter++;
			}else if(rtnLink->e.itemCode == 8){ //mat = 8 is aluminum
				desiredPosition =  alPosition;
				alCounter++;
			}//else if
			
			
			//This contains the algorithm for the minimum rotation needed to reach the desired position. It will result in the motor turning either
			//0 steps, 50 steps, or 100 steps. It ensures that if an object is 50 steps away but is a 150 step difference it still only turns 50 */
			if(desiredPosition == currentPosition){
				minSteps = 0; // Reset the minsteps needed to reach the desired position. As minSteps is zero, this indicates the previous object is the same as the current one
			}else if((abs(desiredPosition - currentPosition) > 100) && ((desiredPosition - currentPosition) < 0)){ //if the desired position number would result in a rotation greater than 100 steps
				directionCW = 0; //for clockwise rotation. This is used for the -50 case (which is faster than +150)
				minSteps = 50;
			}else if((desiredPosition - currentPosition) < 100){
				if((desiredPosition - currentPosition) < 0){
					directionCW = 1; //for counterclockwise rotation
				}else{
					directionCW = 0; //for counterclockwise rotation
				}//else
				minSteps = (abs(desiredPosition - currentPosition));
			}else if((desiredPosition - currentPosition) == 100) {
				directionCW = 1; //for clockwise rotation
				minSteps = (abs(desiredPosition - currentPosition));
			}else if((desiredPosition - currentPosition) > 100) {
				directionCW = 1; //for clockwise rotation
				minSteps = (abs(desiredPosition - 100));
			}//else if
			
			
			int stepCounter; // Counter for number of motor steps declared in main
			int i; // Variable used for iteration through acceleration array
			
			//This is the case where the sorting tray is on the desired position. Immediately begin the conveyor motor.
			if(minSteps == 0){
				PORTB = counterClockwise;
			}//if

			//For clockwise rotation of stepper
			if (directionCW == 1){ 
				
				while(stepCounter < minSteps){ //run until desired steps is met
					stepperStagePosition++; //takes current stepper position and adds one to move to next step (clockwise)
					
					//if the rotation is 50. Else is used for the 100 case as these are the only two amounts of rotation needed
					if(minSteps == 50){
						if (stepCounter < 10){ //First 10 steps include acceleration profile
							velt = accelArray[i]; //Sets variable for the time between steps. Accessed different values in array which were decided from testing protocol
							i++; // Iterate for accessing next location in array
							timeVel = velt; //Store corresponding acceleration time in varible that will be passed to a timer
						}else if(stepCounter > (40)){ //Final 10 steps include deceleration profile
							velt = accelArray[i];
							i--; // Same profile as acceleration, just in opposite order
							timeVel = velt;
						}else{ // Run when constant max velocity is desired
							velt = 6200; // min time delay (ms) between coil firings
							timeVel = velt;
						}//else
					}else{ //For 100 step rotation case
						if(stepCounter < 10){ //First 10 steps include acceleration profile
							velt = accelArray[i];
							i++;
							timeVel = velt;
						} else if (stepCounter > (90)){ //Final 10 steps include deceleration profile
							velt = accelArray[i];
							i--;
							timeVel = velt;
						}else{
							velt = 6200;
							timeVel = velt;
						}//else
					}//else
				
					 //This is used to time the object to land on the sorting tray as close to the center as possible.
					 //10 steps away from the desired position, the conveyor begins to rotate and allows the object to land in center.
					 //Increasing the value too much causes continuous loading issues
					if((minSteps - stepCounter) == 10){
						PORTB = counterClockwise;
					}//if

					if(stepperStagePosition == 5){ //All four steps have been run, restart coil
						stepperStagePosition = 1;
					}//if
					
					//For the first iteration of the stepper function, while there are steps left
					if(stepCounter != 0){
						PORTL = 0x10; //Sets LED for visual confirmation
						while(crankTimer_result_flag == 0); //Waits for the result flag to be set which only occurs once the interrupt is fired and the desired time is reached
							crankTimer_result_flag = 0; // resets result flag
					}//if

					//For stepper coil firing sequence
					if(stepperStagePosition == 1){ //Step 1 activate
						PORTA = Step1;
						crankTimer(timeVel); //Passes time delay into microsecond timer 
					}else if(stepperStagePosition == 2){ //Step 2 activate
						PORTA = Step2;
						crankTimer(timeVel); 
					}else if(stepperStagePosition == 3){ //Step 3 activate
						PORTA = Step3;
						crankTimer(timeVel);
					}else if(stepperStagePosition == 4){ //Step 4 activate
						PORTA = Step4;
						crankTimer(timeVel);
					}//else if
					stepCounter++; //Used to count each step until total amount of steps has been reached
					crankTimer_result_flag = 0; //Reset interrupt flag
				}//while
				
				currentPosition = desiredPosition; //Sets the position rotated to the new current position of steps 
				//Reset flags
				stepperStageFlag = 0;
				INT2_result_flag = 0;
					
				//If an object is sensed by the optical sensor at the reflective stage, begin a new ADC conversion
				//This was implemented for continuous loading as the ADC stops while the stepper is moving but needs to restart
				if((PIND&0b00001000) == 0x08){
					ADCSRA |= _BV(ADSC); //new ADC conversion
				}//if
			}//if - clockwise direction
			
			//For counter clockwise rotation of stepper, the block is identical to the clockwise shown above minus self descriptive direction changes
			//and thus comments are not required for this section. If more clarification is needed refer to the clockwise direction 
			if (directionCW == 0){ 

				while(stepCounter < minSteps){
					
					if(minSteps == 50){
						if (stepCounter < 10){
							velt = accelArray[i];
							i++;
							timeVel = velt;
						}else if(stepCounter > (40)){
							velt = accelArray[i];
							i--;
							timeVel = velt;
						}else{
							velt = 6200;
							timeVel = velt;
						}//else 
					}else{ //For 100 steps
						if(stepCounter < 10){
							velt = accelArray[i];
							i++;
							timeVel = velt;
						}else if(stepCounter > (90)){
							velt = accelArray[i];
							i--;						
							timeVel = velt;
						}else{
							velt = 6200;
							timeVel = velt;
						}//else 
					}//else
					
					if((minSteps - stepCounter) == 10){
						PORTB = counterClockwise;
					}//if
					
					if(stepperStagePosition == 0){ //All four steps have been run, restart coil
						stepperStagePosition = 4;
					}//if

					if(stepCounter != 0){
						PORTL = 0x10;
						while(crankTimer_result_flag == 0);
							crankTimer_result_flag = 0;		
					}//if

					//Stepper coil firing sequence
					if(stepperStagePosition == 4){ //Step 1 activate
						PORTA = Step4;
						crankTimer(timeVel);
					}else if(stepperStagePosition == 3){ //Step 2 activate
						PORTA = Step3;
						crankTimer(timeVel);
					}else if(stepperStagePosition == 2){ //Step 3 activate
						PORTA = Step2;
						crankTimer(timeVel);
					}else if(stepperStagePosition == 1){ //Step 4 activate
						PORTA = Step1;
						crankTimer(timeVel);
					}//else if
					
					stepCounter++; //used to count each step until total amount of steps has been reached
					stepperStagePosition--; //takes current position and subtracts one to move to next step
					crankTimer_result_flag = 0; //resets timer flag
					}//while
					
					currentPosition = desiredPosition; //Sets the position rotated to the new current
					//Reset flags
					stepperStageFlag = 0;
					INT2_result_flag = 0;
					
					//If an object is sensed by the optical sensor at the reflective stage, begin a new ADC conversion
				    //This was implemented for continuous loading as the ADC stops while the stepper is moving but needs to restart
					if((PIND&0b00001000) == 0x08){
						ADCSRA |= _BV(ADSC); //new ADC conversion
					}//if
						
			}//if - Counter clockwise
			
			velt = 20500; //Resets time between coil firings to the largest val
			stepperStageFlag = 0; //reset flag
			
			//This is true once the ramp down button is triggered. Will enter as eStop flag is set to one in the interrupt
			if(eStop == 1)
				killProgram++; //Increments how many object are left
		}//if from main
		

		//Loop containing the instructions for the ramp down function. Occurs after the final object detected by primary optical sensor
		while(killProgram == 2){
			mTimer(5000); // wait 5s
			PORTB = stop; //Stop conveyor
			cli(); //Diable all interrupts
			LCDClear();
			
			//Contains content for LCD display (qty. of sorted components)
			LCDWriteStringXY(0,0,"BL WH ST AL"); 
			LCDWriteIntXY(0,1,blCounter, 2); 
			LCDWriteIntXY(3,1,whCounter, 2); 
			LCDWriteIntXY(6,1,stCounter, 2);
			LCDWriteIntXY(9,1,alCounter, 2); 	
		}//while
	}// while - true
}//end Main


//Interrupt Vector for pause button, Active Low. Program enters this once the button is pressed
//Outputs total sorted quantity of each type of object and amount in queue. Program clears LCD and 
//resumes sorting process when pushed again 
ISR(INT0_vect){ 
	mTimer(20); //debounce
	if((0b00000001&PIND) == 0){
		if(pauseButton == 1){ //pause
			PORTB = stop; //Stops DC motor
			
			//LCD printing for qty. sorted
			LCDWriteStringXY(0,0,"BL WH ST AL PS"); 
			LCDWriteIntXY(0,1,blCounter, 2); 
			LCDWriteIntXY(3,1,whCounter, 2); 
			LCDWriteIntXY(6,1,stCounter, 2);
			LCDWriteIntXY(9,1,alCounter, 2); 
			LCDWriteIntXY(12,1,size(&head, &tail), 2); //For number of items in linkedlist (detected but not sorted)
			pauseButton = 0; //set to zero so next push resumes belt
		}else{ //resume
			LCDClear(); //Clear the screen
			pauseButton = 1; //Set to 1 so next push is pause belt
			PORTB = counterClockwise;
		}//else
	}//if
	
	//debounce
	while((0b00000001&PIND) == 0); 
		mTimer(20);
}//INT0


//Interrupt Vector for Ramp down, Active HIGH. Program enters this once the button is depressed. Ramp down consists of sorting the remaining
//items on the belt and fully stopping all interrupts and motors. In this ISR, a flag is set which is then used in main to continue the ramp down process
ISR(INT1_vect){ 
	//Debounce. Note the extra if statement was added as without it the debouncing was unreliable
	mTimer(20);
	if((0b00000010&PIND) == 1){
	} //if
	eStop = 1; // Sets flag that ramp down is active. This corresponds to a statement in main
	while((0b00000010&PIND) == 1); //Debounce
		mTimer(20);
}//INT1


//Interrupt vector for sensor at end of apparatus (gate optical sensor) TX119. This indicates that a part needs to be sorted.
//The flags set in the function are required in main to tell the motor to rotate once an object is detected
ISR(INT2_vect){
	
	//Enters loop if first object in line. Only exception to this would be if the motor is not done spinning but another object triggers 
	//this interrupt. This is required when reducing rim hits by starting the DC motor before the stepper is done rotating
	if(stepperStageFlag == 0){ 
		PORTB = stop; //Stop motor;
		
		//Set flags to true indicating object has been sensed
		INT2_result_flag = 1;
		stepperStageFlag = 1;
	}else{ //This else is for the case where the stepperStageFlag != 0, which is indicative of the two objects being of the same type/colour
		PORTB = stop; //Stop motor;
		stepsLeft = 1; //Flag for case of multiple objects of same type in a row
	}//else
}//INT2


//Interrupt vector for optical sensor in middle of apparatus. This indicates that a part needs to categorized. Once we know an object is in front of the sensor
//it needs to begin to read the analog values associated with its reflectivity and create a new link to store this objects corresponding info (material, count)
ISR(INT3_vect){
	ADCSRA |= _BV(ADSC); //Starts new ADC conversion
	minVal = 1024; //This is value read if no object is detected (absolute min)
	initLink(&newLink);	//Initialize new link
}//INT3


//Interrupt vector for microsecond timer. This is entered once the timer reaches the set counter number
ISR(TIMER3_COMPA_vect){ //
	crankTimer_result_flag = 1; // Sets flag which indicates timer is complete for expression in main
	TIMSK3 &= ~(_BV(OCIE3A)); // This enables Interrupt. 0x01 is 2nd bit which is output (note this corresponds with interrupt vector)
}//TIMER3


//Interrupt vector is entered once ADC is done. It compares past ADC values to new updated ones to find a minimum. Once the minimum has been found
//it classifies the material based off the sensor value, and adds to queue. Then returns to main
ISR(ADC_vect){
	if(minVal >= ADC){ //if minVal is greater than ADC result, update minVal.
		minVal = ADC; //Will run until minVal < ADC Result. This will then be the last value (MIN VAL)
	}
	
	//This if statement accounts for the case of an object being in front of the primary optical sensor but no object in front of the gate sensor and is needed for continuods loading
	if(((PIND&0b00001000) == 0x08) && (INT2_result_flag == 0)) { 
		ADCSRA |= _BV(ADSC); //new ADC conversion
	}else if(minVal >= 915){ //Range for black object
		mat = 5; //mat = 5 is black
		newLink->e.itemCode = mat; //Create new link with material type
		enqueue(&head,&tail,&newLink); //Enqueue new link into linkedlist
	}else if(minVal >= 750 && minVal < 915){ //Range for white object 
		mat = 6; //mat = 6 is white
		newLink->e.itemCode = mat;
		enqueue(&head,&tail,&newLink);
	}else if(minVal < 750 && minVal > 300){ //Range for steel object
		mat = 7; //mat = 7 is steel
		newLink->e.itemCode = mat;
		enqueue(&head,&tail,&newLink);
	}else if(minVal <= 300){ //Range for aluminum object
		mat = 8; //mat = 8 is aluminum
		newLink->e.itemCode = mat;
		enqueue(&head,&tail,&newLink);
	 }//else if
}//ADC Vect


//Creates PWM waveform at Port B7. Once called, output is continuous PWM. Run at 488Hz
void PWMgen (){
	TCCR0A |= _BV(WGM01) | _BV(WGM00); //Sets timer to Fast PWM Mode (WGM02 is already at 0)
	TCCR0A |= _BV(COM0A1); //Clear OC0A on Compare Match, set OC0A at BOTTOM (non-inverting mode)
	TCCR0B |= _BV(CS01) | _BV(CS00); // prescale 64 to achieve 488Hz
	OCR0A = 0x66; //40% duty cycle. (0xFF = 255 decimal (max), and 0x66 = 102 decimal
	DDRB = 0xFF; // OCR0A in PORTB
}


//Clock function that is a millisecond timer. Takes in input of time to wait. Prescaled to 8MHz
void mTimer(int count){
	int i; // Tracks loop number
	i = 0; // Initializing counter to 0
	TCCR1B |= _BV(CS11); // Sets second bit to 1 in timer counter control register
	TCCR1B |= _BV(WGM12); // Sets all bits in register location WGM12 to 1
	OCR1A = 1000; // Sets output compare register for 1000. This is setting the upper limit to 1000
	TCNT1 = 0x0000; // Sets timer counter to 0
	TIFR1 |= _BV(OCF1A); // Sets the timer register interrupt flag to 1. This is needed as without it, the interrupt flag could start at an arbitrary point

	// Loop checks if the timer has reached the maximum set value of 0x03E8
	while(i<count){
		if((TIFR1 & 0x02) == 0x02){
			TIFR1 |= _BV(OCF1A); // Clears interrupt flag by setting the bit value to 1.
			i++; // Increment counter variable for loop
		} //if
	} //while
}//mTimer
		
//Clock function that is a microsecond timer. Takes in input of time to wait. Prescaled to 8MHz
//Utilizing a microsecond timer allows for more accuracy with regards to the integer passed through. We can add
//3 digits of precision compared to mTimer. This also functions using an interrupt as opposed to an in-function- while loop 
void crankTimer(unsigned int count){
	TCCR3B |= _BV(WGM32); // Sets all bits in register location WGM32 to 1
	OCR3A = count; // Sets output compare register to input value
	TCNT3 = 0x0000; // Sets timer counter to 0
	TIFR3 |= _BV(OCF3A); //Clears timer interrupt flag by setting the timer register interrupt flag to 1. This is needed as without it, the interrupt flag could start at an arbitrary point
	TIMSK3 |= _BV(OCIE3A); // This enables Interrupt. 0x01 is 2nd bit which is output (note this corresponds with interrupt vector)
	TCCR3B |= _BV(CS31); //starts timer
} //crankTimer


/**************************************************************************************/
/***************************** Linked List Functions **********************************/
/**************************************************************************************/


/**************************************************************************************
* DESC: initializes the linked queue to 'NULL' status
* INPUT: the head and tail pointers by reference
*/

void setup(link **h,link **t){
	*h = NULL;		/* Point the head to NOTHING (NULL) */
	*t = NULL;		/* Point the tail to NOTHING (NULL) */
	return;
}/*setup*/


/**************************************************************************************
* DESC: This initializes a link and returns the pointer to the new link or NULL if error 
* INPUT: the head and tail pointers by reference
*/
void initLink(link **newLink){
	//link *l;
	*newLink = malloc(sizeof(link));
	(*newLink)->next = NULL;
	return;
}/*initLink*/


/****************************************************************************************
*  DESC: Accepts as input a new link by reference, and assigns the head and tail		
*  of the queue accordingly				
*  INPUT: the head and tail pointers, and a pointer to the new link that was created 
*/
/* will put an item at the tail of the queue */
void enqueue(link **h, link **t, link **nL){
	if (*t != NULL){
		/* Not an empty queue */
		(*t)->next = *nL;
		*t = *nL; //(*t)->next;
	}/*if*/
	else{
		/* It's an empty Queue */
		//(*h)->next = *nL;
		*h = *nL;
		*t = *nL;
	}/* else */
	return;
}/*enqueue*/


/**************************************************************************************
* DESC : Removes the link from the head of the list and assigns it to deQueuedLink
* INPUT: The head and tail pointers, and a ptr 'deQueuedLink' 
* 		 which the removed link will be assigned to
*/
/* This will remove the link and element within the link from the head adn tail of the queue */
void dequeue(link **h, link **t, link **deQueuedLink){
	/* ENTER YOUR CODE HERE */
	*deQueuedLink = *h;	// Will set to NULL if Head points to NULL
	/* Ensure it is not an empty queue */
	if (*h != NULL){
		*h = (*h)->next;
	}/*if*/
	//Removes tail
	if (*h == NULL){
		*t = NULL;
		}/*if*/	
	return;
}/*dequeue*/


/**************************************************************************************
* DESC: Peeks at the first element in the list
* INPUT: The head pointer
* RETURNS: The element contained within the queue
*/
/* This simply allows you to peek at the head element of the queue and returns a NULL pointer if empty */
element firstValue(link **h){
	return((*h)->e);
}/*firstValue*/


/**************************************************************************************
* DESC: deallocates (frees) all the memory consumed by the Queue
* INPUT: the pointers to the head and the tail
*/
/* This clears the queue */
void clearQueue(link **h, link **t){

	link *temp;

	while (*h != NULL){
		temp = *h;
		*h=(*h)->next;
		free(temp);
	}/*while*/
	
	/* Last but not least set the tail to NULL */
	*t = NULL;		

	return;
}/*clearQueue*/


/**************************************************************************************
* DESC: Checks to see whether the queue is empty or not
* INPUT: The head pointer
* RETURNS: 1:if the queue is empty, and 0:if the queue is NOT empty
*/
/* Check to see if the queue is empty */
char isEmpty(link **h){
	/* ENTER YOUR CODE HERE */
	return(*h == NULL);
}/*isEmpty*/



/**************************************************************************************
* DESC: Obtains the number of links in the queue
* INPUT: The head and tail pointer
* RETURNS: An integer with the number of links in the queue
*/
/* returns the size of the queue*/
int size(link **h, link **t){
	link 	*temp;			/* will store the link while traversing the queue */
	int 	numElements;
	numElements = 0;
	temp = *h;			/* point to the first item in the list */
	while(temp != NULL){
		numElements++;
		temp = temp->next;
	}/*while*/
	
	return(numElements);
}/*size*/