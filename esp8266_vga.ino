#include <ESPVGAX.h>
#include <SoftwareSerial.h>

// VGA resolution is 512x480 (1bpp)
ESPVGAX vga;

// Software Serial on D2 (RX) and D1 (TX)
SoftwareSerial swSerial(4, 5); 

int x_pos = 0;
int last_y = 0;

void setup() {
  // Initialize VGA (must be done before other hardware)
  vga.begin();
  vga.clear(0); // Clear screen to black

  // Initialize Software Serial (keep baud rate low for stability)
  swSerial.begin(9600);
}

void loop() {
  if (swSerial.available()) {
    // Read line until newline (standard CSV format)
    String data = swSerial.readStringUntil('\n');
    
    // Simple CSV parsing: find the first comma
    int commaIndex = data.indexOf(',');
    String valStr = (commaIndex > -1) ? data.substring(0, commaIndex) : data;
    
    int val = valStr.toInt();
    
    // Map value to screen height (0-1023 input to 0-479 pixels)
    int y_pos = map(val, 0, 1023, 479, 0);

    // Draw line from last point to current point
    if (x_pos > 0) {
      // Note: ESPVGAX drawLine(x1, y1, x2, y2, color)
      // Since it's 1bpp, color 1 is 'on'
      for(int i = min(last_y, y_pos); i <= max(last_y, y_pos); i++) {
          vga.putpixel(x_pos, i, 1);
      }
    }

    last_y = y_pos;
    x_pos++;

    // Reset screen when edge is reached
    if (x_pos >= 512) {
      x_pos = 0;
      vga.clear(0);
    }
  }
}
