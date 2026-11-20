// OpenPelletBridge controller board (40 x 57 mm) - 3D printable case.
//
//   openscad -D 'part="base"' -o base.stl openpelletbridge-case.scad
//   openscad -D 'part="lid"'  -o lid.stl  openpelletbridge-case.scad
//   part = "assembly" (default) shows both, with the board (board.stl) if present.
//
// No screws. The board drops onto four standoffs whose pegs go into its M2 holes (it cannot
// shift), and the lid's four clamping tubes press it onto the standoffs. The lid clicks into
// the base with four snap hooks (two per long side) that catch in windows in the walls; to
// open, push the hooks in through the windows with a small screwdriver and lift the lid.
//
// In the lid: two flexure buttons (BOOT, RESET) with a plunger onto the tactile switches,
// light tubes above the four LEDs (SYS, STOVE, TX, RX; print the lid in a translucent
// filament, or fill the holes with clear resin / a 2 mm light pipe), sound holes above the
// buzzer and engraved labels. In the base: an opening for the USB-C plug and a notch for
// the stove connector's cable plug.
//
// Print: base upright, lid upside down (top face on the bed), 0.2 mm layers, no supports.
// Plastic only near the antenna (top edge): do not add metal there.
//
// Board coordinates below come from hardware/tools/gen_pcb.py (x right, y down from the
// top edge, the antenna side); heights from the board's 3D model (top of the PCB = 0).

part = "assembly";      // "base", "lid" or "assembly"

/* [Board] */
pcb_w = 40.0;           // board width (x)
pcb_h = 57.0;           // board length (y)
pcb_t = 1.6;
holes = [[3.0, 28.0], [37.0, 28.0], [2.4, 54.6], [37.6, 54.6]];   // M2, board coordinates

/* [Case] */
gap        = 0.5;       // board edge to inner wall
wall       = 2.0;
floor_t    = 1.6;
lid_t      = 1.6;
standoff_h = 4.0;       // floor to board bottom (stove connector pins: 2.1 mm below the board)
above      = 7.6;       // board top to the lid's underside (stove connector: 7.0 mm)
lip        = 1.5;       // alignment lip of the lid, inside the walls
fit        = 0.2;       // clearance lip / walls
standoff_d = 5.0;
peg_d      = 1.9;       // locating pegs in the board's 2.2 mm holes
peg_h      = 1.3;       // shorter than the board (1.6 mm): the tubes press on the board, not on the pegs
snap_y     = [12.0, 44.0];  // snap hooks on both long sides, board y (clear of the parts along the edges)
snap_w     = 6.0;       // hook width
snap_t     = 1.0;       // hook arm thickness
snap_len   = 6.5;       // arm length below the lid (ends 1.1 mm above the board, over its empty edge strip)
snap_hook  = 0.7;       // hook depth (0.5 mm catch: 0.2 mm is the lip clearance)
tube_d     = 4.4;       // lid clamping tubes (copper-free ring on the board: 4.8 mm)
ears       = true;      // mounting ears with a 4.2 mm hole on both long sides
$fn = 48;

/* [Parts on the board] */
buttons = [[6.2, 7.6, "BOOT"], [32.7, 7.6, "RESET"]];
button_top = 1.51;      // tactile switch top above the board
leds = [[31.9, 20.1, 0.83, 2.0, "SYS", [-3.6, 0]], [35.1, 20.1, 0.83, 2.0, "STOVE", [0, -3.0]],
        [7.4, 30.3, 1.10, 1.6, "TX", [0, -3.0]], [13.8, 30.3, 1.10, 1.6, "RX", [0, -3.0]]];   // x, y, top, hole, label, label offset
buzzer = [20.0, 28.6];
usb_x = 29.5;  usb_z = 1.65;          // USB-C centre (x, height above the board top)
usb_open = [13.0, 7.5];               // opening for the cable's overmould
j2_x = [4.6, 20.7];                   // stove connector width (header 5.15 - 20.15)

// derived
in_w = pcb_w + 2 * gap;
in_h = pcb_h + 2 * gap;
out_w = in_w + 2 * wall;
out_h = in_h + 2 * wall;
board_z = floor_t + standoff_h;                  // bottom of the board
top_z = board_z + pcb_t;                         // top of the board
wall_h = top_z + above;                          // base height = lid underside
function bx(x) = x;                              // board x -> case x (origin: board corner)
function by(y) = pcb_h - y;                      // board y (down) -> case y (up)

module rbox(w, h, t, r = 2) {
    linear_extrude(t) offset(r) offset(-r) square([w, h]);
}

module base() {
    difference() {
        union() {
            translate([-gap - wall, -gap - wall, 0]) rbox(out_w, out_h, wall_h, 3);
            if (ears) for (sx = [0, 1]) translate([sx ? pcb_w + gap + wall - 1 : -gap - wall - 8 + 1, pcb_h / 2 - 6, 0])
                rbox(8, 12, floor_t + 1, 2);
        }
        // inside
        translate([-gap, -gap, floor_t]) rbox(in_w, in_h, wall_h, 1);
        // USB-C opening (bottom edge, case y = 0 side)
        translate([bx(usb_x) - usb_open[0] / 2, -gap - wall - 1, top_z + usb_z - usb_open[1] / 2])
            cube([usb_open[0], wall + 2, usb_open[1]]);
        // stove connector: notch from the board top up, for the cable plug
        translate([bx(j2_x[0]), -gap - wall - 1, top_z - 0.3]) cube([j2_x[1] - j2_x[0], wall + 2, above + 1]);
        // windows for the snap hooks (the catch is the window's top edge)
        for (y = snap_y, sx = [0, 1]) translate([sx ? pcb_w + gap - 0.01 : -gap - wall - 1, by(y) - snap_w / 2 - 0.3, wall_h - snap_len - 0.2])
            cube([wall + 1.01, snap_w + 0.6, 1.6]);
        // ear holes
        if (ears) for (sx = [0, 1]) translate([sx ? pcb_w + gap + wall + 3 : -gap - wall - 3, pcb_h / 2, -1])
            cylinder(d = 4.2, h = 10);
    }
    // standoffs with locating pegs
    for (h = holes) translate([bx(h[0]), by(h[1]), floor_t - 0.01]) {
        cylinder(d = standoff_d, h = standoff_h + 0.01);
        cylinder(d = peg_d, h = standoff_h + peg_h, $fn = 24);
        translate([0, 0, standoff_h + peg_h]) cylinder(d1 = peg_d, d2 = peg_d - 0.6, h = 0.3, $fn = 24);
    }
}

module lid() {
    // built in place (top of the walls), printed upside down
    translate([0, 0, wall_h]) difference() {
        union() {
            translate([-gap - wall, -gap - wall, 0]) rbox(out_w, out_h, lid_t, 3);
            // alignment lip inside the walls (open at the connector side)
            translate([0, 0, -lip]) difference() {
                translate([-gap + fit, -gap + fit, 0]) rbox(in_w - 2 * fit, in_h - 2 * fit, lip, 1);
                translate([-gap + fit + 1.2, -gap + fit + 1.2, -1]) rbox(in_w - 2 * fit - 2.4, in_h - 2 * fit - 2.4, lip + 2, 0.5);
                translate([-gap - 1, -gap - 1, -1]) cube([in_w + 2, 4, lip + 2]);
                // room for the snap arms to flex
                for (y = snap_y) translate([-gap - 1, by(y) - snap_w / 2 - 0.6, -1]) cube([in_w + 2, snap_w + 1.2, lip + 2]);
            }
            // snap hooks
            for (y = snap_y, sx = [0, 1]) translate([sx ? pcb_w + gap - fit : -gap + fit, by(y), 0])
                mirror([sx, 0, 0]) snap_arm();
            // clamping tubes: press the board onto the standoffs
            for (h = holes) translate([bx(h[0]), by(h[1]), -above]) cylinder(d = tube_d, h = above + 0.01);
            // light tubes down to just above the LEDs
            for (l = leds) translate([bx(l[0]), by(l[1]), -(above - l[2] - 0.4)]) cylinder(d = l[3] + 1.0, h = above - l[2] - 0.4 + 0.01);   // SYS / STOVE are 3.2 mm apart
            // button plungers (on the flexure tongues)
            for (b = buttons) translate([bx(b[0]), by(b[1]), -(above - button_top - 0.3)]) cylinder(d = 3, h = above - button_top - 0.3 + 0.01);
        }
        // LED holes
        for (l = leds) translate([bx(l[0]), by(l[1]), -above]) cylinder(d = l[3], h = above + lid_t + 1);
        // flexure tongues: U-shaped slot around each button, hinge towards the board centre
        for (b = buttons) translate([bx(b[0]), by(b[1]), -1]) linear_extrude(lid_t + 2) difference() {
            translate([-3.9, -9.5]) square([7.8, 12.9]);
            translate([-3.1, -9.6]) square([6.2, 12.2]);
        }
        // buzzer sound holes
        for (a = [0 : 60 : 359]) translate([bx(buzzer[0]) + 1.6 * cos(a), by(buzzer[1]) + 1.6 * sin(a), -1]) cylinder(d = 1.0, h = lid_t + 2);
        translate([bx(buzzer[0]), by(buzzer[1]), -1]) cylinder(d = 1.0, h = lid_t + 2);
        // stove connector: the notch goes through the lid as well, up to the header's front
        // (case y 0.5), so a cable plug of any height fits
        translate([bx(j2_x[0]), -gap - wall - 1, -lip - 1]) cube([j2_x[1] - j2_x[0], wall + gap + 1.9, lip + lid_t + 2]);
        // engraved labels (0.5 mm deep)
        translate([0, 0, lid_t - 0.5]) linear_extrude(1) {
            for (b = buttons) translate([bx(b[0]), by(b[1]) + 5.4]) text(b[2], size = 2.2, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
            for (l = leds) translate([bx(l[0]) + l[5][0], by(l[1]) + l[5][1]]) text(l[4], size = l[4] == "STOVE" ? 1.8 : 2.0, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
            translate([bx(20), by(41.5)]) text("OpenPelletBridge", size = 3.2, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
            translate([bx(20), by(46.5)]) text("Micronova serial", size = 2.2, halign = "center", valign = "center", font = "Liberation Sans");
            translate([bx(12.65), by(54.0)]) text("STOVE", size = 2.0, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
            translate([bx(usb_x), by(54.0)]) text("USB-C", size = 2.0, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
        }
    }
}

// snap arm, outer face at x = 0 (towards the wall), hanging down from the lid's underside
module snap_arm() {
    translate([0, -snap_w / 2, 0]) rotate([90, 0, 0]) mirror([0, 0, 1]) linear_extrude(snap_w)
        polygon([[0, 0.01], [snap_t, 0.01], [snap_t, -snap_len], [0, -snap_len],
                 [-snap_hook, -snap_len + 0.8], [-snap_hook, -snap_len + 1.4], [0, -snap_len + 1.4]]);
}

module board() {
    // board.stl: the board's 3D model (exported from KiCad), origin at the board's top-left
    // corner, PCB bottom at z = 0; only used for the assembly view
    translate([0, 0, board_z]) import("board.stl");
}

if (part == "base") base();
else if (part == "lid") rotate([180, 0, 0]) translate([0, 0, -wall_h - lid_t]) lid();   // top face on the bed
else {
    color("SlateGray") base();
    color("WhiteSmoke", 0.6) lid();
    color("DarkGreen") board();
}
