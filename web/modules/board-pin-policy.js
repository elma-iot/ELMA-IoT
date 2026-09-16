// Chip capabilities, not display-name substring guesses or contiguous GPIO ranges.
export function boardChipFamily(board = "") {
  if (board === "custom-board") {
    try { return JSON.parse(localStorage.getItem("elma.custom.board") || "{}").chipFamily || "esp32"; }
    catch { return "esp32"; }
  }
  if (board === "esp32-spk-n16r8" || board.startsWith("esp32-s3")) return "esp32s3";
  if (board.startsWith("esp32-s2")) return "esp32s2";
  if (board.startsWith("esp32-c2")) return "esp32c2";
  if (board === "esp32-c3") return "esp32c3";
  if (board === "esp32-c6") return "esp32c6";
  if (board.startsWith("esp8266")) return "esp8266";
  if (board.startsWith("esp8285")) return "esp8285";
  return "esp32";
}
export function supportedBoard(board) {
  return !["esp32s2", "esp32c6"].includes(boardChipFamily(board));
}
export function chipPins(chip, output = false, board = "") {
  let pins = [];
  if (chip === "esp32") pins = [0,1,2,3,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33,34,35,36,39];
  if (chip === "esp32s3") pins = [...Array.from({length:22}, (_,i)=>i), ...Array.from({length:16}, (_,i)=>i+33)];
  if (chip === "esp32c3") pins = [...Array.from({length:11}, (_,i)=>i),20,21];
  if (chip === "esp32s2") pins = [...Array.from({length:22}, (_,i)=>i),26,...Array.from({length:14}, (_,i)=>i+33)];
  if (chip === "esp32c2") pins = [...Array.from({length:11}, (_,i)=>i),18,19,20];
  if (chip === "esp32c6") pins = [...Array.from({length:24}, (_,i)=>i)];
  if (chip === "esp8266" || chip === "esp8285") pins = [0,1,2,3,4,5,12,13,14,15,16];
  return pins.filter(pin => !(output && ((chip === "esp32" && pin >= 34) || (chip === "esp32s3" && pin === 46))))
    .filter(pin => !(board === "esp32-wrover" && [16,17].includes(pin)))
    .filter(pin => !(["esp32-s3-psram","esp32-spk-n16r8","esp32-s3-devkit-c1","esp32-s3-cam-module"].includes(board) && [33,34,35,36,37].includes(pin)));
}
export function pinChoices(pins, selected, blocked = new Set()) {
  const value = String(selected ?? "");
  const choices = pins.filter(pin => !blocked.has(pin)).map(pin => ({value:String(pin), label:`GPIO${pin}`, disabled:false}));
  if (value !== "" && !choices.some(option => option.value === value)) {
    choices.unshift({value, label:`GPIO${value} — ${pins.includes(Number(value)) ? "assigned to another function" : "invalid/reserved for this board"} (reassign)`, disabled:true});
  }
  return choices;
}
export function applyBoardFilter(board, chip, autodetect) {
  for (const option of board.options) {
    const supported = supportedBoard(option.value);
    option.disabled = !supported || (autodetect && chip !== "auto" && boardChipFamily(option.value) !== chip);
    option.title = !supported ? "No firmware build target is available for this chip." : option.disabled ? "Different target chip; untick board autodetect for manual selection." : "";
  }
  if (autodetect && board.selectedOptions[0]?.disabled) {
    const usable = [...board.options].find(option => !option.disabled);
    if (usable) { board.value = usable.value; board.dispatchEvent(new Event("change", {bubbles:true})); }
  }
}
