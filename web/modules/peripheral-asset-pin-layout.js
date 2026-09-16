import { canonicalSignalKey } from "./peripheral-pin-model.js";

// Contact coordinates are normalized to each SVG viewBox. They describe the
// copper/header contact in the existing artwork, not a decorative edge of the
// image. Aliases let the electrical model use canonical application names even
// when a board silkscreen uses BCK/LCK/VDD/VIN.
const ASSET_CONTACTS = {
  "pcm5102a-breadboard.svg": {
    SCK: [0.0445, 0.1021, "left"],
    BCLK: [0.0445, 0.2373, "left"],
    DIN: [0.0445, 0.3729, "left"],
    WS: [0.0445, 0.5085, "left"],
    GND: [0.0445, 0.6441, "left"],
    VCC: [0.0445, 0.7797, "left"],
    VIN: [0.0445, 0.7797, "left"],
  },
  "max98357a-breadboard.svg": {
    WS: [0.0714, 0.8670, "bottom"],
    LRC: [0.0714, 0.8670, "bottom"],
    BCLK: [0.2143, 0.8670, "bottom"],
    DIN: [0.3571, 0.8670, "bottom"],
    GAIN: [0.5000, 0.8670, "bottom"],
    SD: [0.6429, 0.8670, "bottom"],
    GND: [0.7857, 0.8670, "bottom"],
    VCC: [0.9286, 0.8670, "bottom"],
    VIN: [0.9286, 0.8670, "bottom"],
  },
  "i2c-oled-breadboard.svg": {
    GND: [0.3585, 0.0610, "top"],
    VCC: [0.4526, 0.0610, "top"],
    SCL: [0.5467, 0.0610, "top"],
    SDA: [0.6408, 0.0610, "top"],
  },
  "inmp441-breadboard.svg": {
    GND: [0.3260, 0.7622, "bottom"],
    SD: [0.4951, 0.7622, "bottom"],
    DOUT: [0.4951, 0.7622, "bottom"],
    VCC: [0.6639, 0.7622, "bottom"],
    SCK: [0.6639, 0.2578, "top"],
    BCLK: [0.6639, 0.2578, "top"],
    WS: [0.4951, 0.2578, "top"],
  },
  "microsd-spi-breadboard.svg": {
    CS: [0.2279, 0.0948, "top"],
    SCK: [0.3337, 0.0948, "top"],
    MOSI: [0.4396, 0.0948, "top"],
    MISO: [0.5454, 0.0948, "top"],
    VCC: [0.6512, 0.0948, "top"],
    GND: [0.7571, 0.0948, "top"],
  },
};

function assetName(source) {
  return String(source || "").split(/[?#]/, 1)[0].split(/[\\/]/).pop().toLowerCase();
}

function rotateSide(side, turns) {
  const sides = ["top", "right", "bottom", "left"];
  const index = sides.indexOf(String(side));
  return index < 0 ? side : sides[(index + turns) % sides.length];
}

function rotateContact(contact, degrees, stagger = 0) {
  let [x, y, side] = contact;
  const turns = ((Math.round(Number(degrees || 0) / 90) % 4) + 4) % 4;
  for (let index = 0; index < turns; index += 1) {
    [x, y] = [1 - y, x];
  }
  return { xFactor: x, yFactor: y, side: rotateSide(side, turns), stagger };
}

export function peripheralAssetPinContact(source, signal, rotation = 0) {
  const contacts = ASSET_CONTACTS[assetName(source)];
  if (!contacts) return null;
  const key = canonicalSignalKey(signal);
  const aliases = {
    "3V3": "VCC",
    "3.3V": "VCC",
    PWR: "VCC",
    LRCLK: "WS",
    LCK: "WS",
    BCK: "BCLK",
    DATA: "DOUT",
  };
  const contact = contacts[key] || contacts[aliases[key]];
  if (!contact) return null;
  const coordinateKey = (entry) => `${entry[0]}:${entry[1]}:${entry[2]}`;
  const uniqueContacts = [...new Map(Object.values(contacts).map((entry) => [coordinateKey(entry), entry])).values()]
    .filter((entry) => entry[2] === contact[2])
    .sort((left, right) => (contact[2] === "left" || contact[2] === "right")
      ? left[1] - right[1]
      : left[0] - right[0]);
  const rank = Math.max(0, uniqueContacts.findIndex((entry) => coordinateKey(entry) === coordinateKey(contact)));
  const side = String(contact[2]);
  const stagger = side === "top" || side === "bottom"
    ? (uniqueContacts.length <= 4 ? rank : rank % 4)
    : rank % 2;
  return rotateContact(contact, rotation, stagger);
}

export function contactLabelLayout(visualRect, label, contact, gap = 4) {
  if (!visualRect?.width || !visualRect?.height || !contact) return null;
  // Small tags remain readable beside dense, calibrated header contacts.
  const rawWidth = Math.max(18, (String(label || "SIG").length * 5) + 8);
  const rawHeight = 14;
  const rotation = 0;
  const width = rawWidth;
  const height = rawHeight;
  const contactX = (Number(contact.xFactor) - 0.5) * visualRect.width;
  const contactY = (Number(contact.yFactor) - 0.5) * visualRect.height;
  let offsetX = 0;
  let offsetY = 0;
  const stagger = Number(contact.stagger || 0);
  if (contact.side === "left") offsetX = -((width / 2) + gap + (stagger * (width + 3)));
  if (contact.side === "right") offsetX = (width / 2) + gap + (stagger * (width + 3));
  const staggerGap = (contact.side === "top" || contact.side === "bottom")
    ? (stagger * (height + 4))
    : 0;
  if (contact.side === "top") offsetY = -((height / 2) + gap + staggerGap);
  if (contact.side === "bottom") offsetY = (height / 2) + gap + staggerGap;
  return {
    xFactor: (contactX + offsetX) / visualRect.width,
    yFactor: (contactY + offsetY) / visualRect.height,
    rotation,
    pinContact: contact,
  };
}

// A readable connector column is the default diagram interface. The SVG
// contact calibration stays available to editors, but wires terminate at
// these labelled connectors rather than disappearing into the artwork.
export function peripheralColumnLabelLayout(ownerRect, boardRect, labels, index) {
  const ownerX=ownerRect.left+ownerRect.width/2,boardX=boardRect.left+boardRect.width/2;
  const direction=boardX>=ownerX?1:-1;
  const width=Math.max(28,...labels.map(label=>String(label).length*6.6+30));
  return {
    xFactor:direction*(0.5+(width/2+8)/ownerRect.width),
    yFactor:((index-(labels.length-1)/2)*28)/ownerRect.height,
    rotation:0,
    labelWidth:width,
    pinContact:{column:true},
  };
}
