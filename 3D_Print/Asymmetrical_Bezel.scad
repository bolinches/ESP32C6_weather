// --- Matching Asymmetrical Bezel ---
board_l = 37.0;    
board_w = 20.0;    
wall = 2.0;        
corner_r = 3.0; // Matches Case Enclosure

lcd_w = 18.5; 
lcd_l = 34.5;

// Outer dimensions to match the case face
case_face_w = board_w + (wall * 2) + 0.5; 
case_face_l = board_l + (wall * 2) + 1.5;

// Screen alignment offset: 3mm USB side vs 1mm other side
// This shifts the window relative to the outer frame
usb_offset = 1.0; 

$fn = 60;

difference() {
    // 1. Outer Frame (Matches Case Face exactly)
    linear_extrude(1.5)
        offset(r = corner_r) 
            square([case_face_w - 2*corner_r, case_face_l - 2*corner_r], center=true);
    
    // 2. Window Cutout (Shifted to create 3mm/1mm asymmetry)
    translate([0, usb_offset, -1])
        linear_extrude(4)
            square([lcd_w, lcd_l], center=true);
}