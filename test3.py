import threading
import time
import sys
import serial

# Configure the serial port for Raspberry Pi 5
# Port /dev/ttyAMA0 maps to GPIO 14 (TX) and GPIO 15 (RX)
PORT = '/dev/ttyAMA0'
BAUDRATE = 9600

def receive_thread(ser):
    """Background thread to continuously listen for incoming data."""
    print("Receiver thread started. Listening for incoming data...\n")
    while ser.is_open:
        try:
            if ser.in_waiting > 0:
                # Read line until newline character \n
                incoming = ser.readline().decode('utf-8', errors='ignore').strip()
                if incoming:
                    # Print without breaking the user prompt layout
                    sys.stdout.write(f"\r[Received]:  {incoming}\r\n")
                    sys.stdout.flush()
        except serial.SerialException:
            break
        time.sleep(0.05)  # Prevent CPU overuse

def main():
    try:
        ser = serial.Serial(
            port=PORT,
            baudrate=BAUDRATE,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS,
            timeout=1
        )
                        
        # Clear data buffers
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Start background reading thread
        reader = threading.Thread(target=receive_thread, args=(ser,), daemon=True)
        reader.start()

        # Main loop for user interaction
        while True:
            
            tx_data = "AT"\r\n"
            print(f"[Sent]: {tx_data}")
                
            
    except serial.SerialException as e:
        print(f"\nError opening or using serial port: {e}")
    except KeyboardInterrupt:
        print("\nProgram interrupted by user.")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
        print("Serial port closed. Goodbye!")

if __name__ == "__main__":
    main()
