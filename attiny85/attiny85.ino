#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

// Mappings for all the pins we actually use (using the PBX notation, not the
// physical pin on the chip)
#define ENCODER_A_IN    2
#define ENCODER_B_IN    1
#define KEY_TRIGGER     0
#define ENCODER_A_OUT   3
#define ENCODER_B_OUT   4
#define KEYBOARD_AWAKE  5

// C macro stuff so you can change pins easily and still arm the right IRQ
#define INTERRUPT_FOR_PIN_HIDDEN(a, b) a##b
#define INTERRUPT_FOR_PIN(pin) INTERRUPT_FOR_PIN_HIDDEN(PCINT, pin)
#define ENCODER_A_INTERRUPT INTERRUPT_FOR_PIN(ENCODER_A_IN)
#define ENCODER_B_INTERRUPT INTERRUPT_FOR_PIN(ENCODER_B_IN)
#define KEYBOARD_AWAKE_INTERRUPT INTERRUPT_FOR_PIN(KEYBOARD_AWAKE)

// This number will continually increase and wrap around - this way if an IRQ
// happens while we're waking up the keyboard we'll detect it later and then
// flush the current state ASAP. It is unlikely we'll get 256 irqs before we
// finish a full loop so this should be sufficient bits.
volatile uint8_t irq_number = 0;

ISR(PCINT0_vect) { irq_number++; }

void setup() {
  // Turnoff the watchdog, brownout and ADC, we don't need them to save more
  // power
  wdt_disable();
  sleep_bod_disable();
  power_adc_disable();
  ADCSRA &= ~(1 << ADEN);

  // The encoder A/B pins need to be pulled up
  pinMode(ENCODER_A_IN, INPUT_PULLUP);
  pinMode(ENCODER_B_IN, INPUT_PULLUP);

  // The output pins where we will mirror the input.
  pinMode(ENCODER_A_OUT, OUTPUT);
  pinMode(ENCODER_B_OUT, OUTPUT);

  // Copy the current value of the encoder across to the output
  digitalWrite(ENCODER_A_OUT, digitalRead(ENCODER_A_IN));
  digitalWrite(ENCODER_B_OUT, digitalRead(ENCODER_B_IN));

  // Arm the input that lets us know if the keyboard is awake.
  pinMode(KEYBOARD_AWAKE, INPUT);

  // Arm the output that lets us wake the keyboard
  pinMode(KEY_TRIGGER, OUTPUT);
  digitalWrite(KEY_TRIGGER, LOW);

  // Turn off interrupts while we arm things
  cli();
  
  PCMSK |= bit(ENCODER_A_INTERRUPT) | bit(ENCODER_B_INTERRUPT) |
           bit(KEYBOARD_AWAKE_INTERRUPT);
  GIMSK |= bit(PCIE);

  // Re-enable them
  sei();
}

uint8_t last_encoder_a_state = 0;
uint8_t last_encoder_b_state = 0;
uint8_t last_keyboard_awake = 0;

uint8_t updated_irq_number = 0;

void loop() {
  // Pause interrupts while we see if we can sleep
  cli();

  // If our local irq number matches the one from the ISR, we can go to sleep
  // because there's nothing to do.
  if (irq_number == updated_irq_number) {
    // Go to deep sleep for maximal power savings.
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);

    // Put our output pins into input mode so they don't do anything
    if (!last_keyboard_awake) {
      pinMode(ENCODER_A_OUT, INPUT_PULLUP);
      pinMode(ENCODER_B_OUT, INPUT_PULLUP);
    }

    sleep_enable();
    // Re-arm interrupts.
    sei();

    // Wait for IRQ
    sleep_cpu();

    // Turn off sleep
    sleep_disable();

    // Awaken the output pins to go back to output mode, and put them into the
    // last state we knew of.
    if (!last_keyboard_awake) {
      pinMode(ENCODER_A_OUT, OUTPUT);
      pinMode(ENCODER_B_OUT, OUTPUT);
      digitalWrite(ENCODER_A_OUT, last_encoder_a_state);
      digitalWrite(ENCODER_B_OUT, last_encoder_b_state);
    }

    // Re-pause the interrupts to we exit this branch as we entered
    cli();
  }

  // By delaying for a bit, we let the signals de-bounce a little
  delayMicroseconds(25);

  // Read all the signals, and grab the current IRQ number so we can track if it
  // changed on us while we are continuing to process stuff.
  uint8_t encoder_a_state = digitalRead(ENCODER_A_IN);
  uint8_t encoder_b_state = digitalRead(ENCODER_B_IN);
  uint8_t keyboard_awake = digitalRead(KEYBOARD_AWAKE);
  updated_irq_number = irq_number;

  // Re-arm interrupts
  sei();

  // Figure out if the encoder changed.
  uint8_t encoder_state_changed = ((encoder_a_state != last_encoder_a_state) ||
                                   (encoder_b_state != last_encoder_b_state));

  // Check the state of the keyboard being awake - if it wasn't, then we need to
  // write to the shift key to bring it back alive.
  if (encoder_state_changed && !keyboard_awake) {
    digitalWrite(KEY_TRIGGER, HIGH);
    delay(5);
    digitalWrite(KEY_TRIGGER, LOW);
    delay(5);

    // We need to simulate the delay of the first rotation, so emit the current
    // value of whatever changed first, then after an appropriate time output
    // the new other value (that'll happen in the main part of this update loop,
    // so we can be lazy and only do one side of it now)
    if (encoder_a_state != last_encoder_a_state) {
      encoder_b_state = digitalRead(ENCODER_B_IN);
      digitalWrite(ENCODER_A_OUT, encoder_a_state);
    } else {
      encoder_a_state = digitalRead(ENCODER_A_IN);
      digitalWrite(ENCODER_B_OUT, encoder_b_state);
    }
    delay(5);
  }
  
  // Update the output state.
  digitalWrite(ENCODER_A_OUT, encoder_a_state);
  digitalWrite(ENCODER_B_OUT, encoder_b_state);

  // Copy the state into the last state for the next loop
  last_encoder_a_state = encoder_a_state;
  last_encoder_b_state = encoder_b_state;
  last_keyboard_awake = keyboard_awake;
}
