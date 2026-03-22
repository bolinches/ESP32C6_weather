// --- Case Enclosure v11 ---
board_l = 37.0;    
board_w = 20.0;    
board_h = 8.0;     
wall = 2.0;        
corner_r = 3.0; // Standard radius for all corners

lcd_w = 18.5; 
lcd_l = 34.5;

case_w = board_w + (wall * 2) + 0.5; 
case_l = board_l + (wall * 2) + 1.5; 
case_h = board_h + wall + 1.0; 

$fn = 60;

difference() {
    // Outer Shell
    linear_extrude(case_h)
        offset(r = corner_r) square([case_w - 2*corner_r, case_l - 2*corner_r], center=true);
    
    // Internal Cavity
    translate([0, 0, wall])
        linear_extrude(case_h)
            square([board_w + 1.0, board_l + 1.0], center=true);
    
    // Front LCD Window
    translate([0, 0, -1])
        cube([lcd_w, lcd_l, wall + 2], center=true);
        
    // USB-C Port (Lowered)
    translate([0, -case_l/2, wall + (board_h/2) - 2]) 
        cube([13, wall*3, 7], center=true);

    // Lateral Vents
    for (i = [-10, 0, 10]) {
        translate([case_w/2, i, wall + (board_h/2)])
            cube([wall*3, 5, 3], center=true);
        translate([-case_w/2, i, wall + (board_h/2)])
            cube([wall*3, 5, 3], center=true);
    }
}