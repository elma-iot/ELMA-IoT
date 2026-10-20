// Prebundle every board variant for the portable compiler, which has no Node
// dependency at runtime. C++ guards select exactly one variant in each image.
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { build } from "esbuild";

const root = fs.realpathSync(path.resolve(process.argv[2]));
const output = path.resolve(process.argv[3]);
const boards = ["miniexco-v2","esp32-2432s028r","esp32-2432s028c","esp32-3248s035c","esp32-cam","esp32-s3-super-mini", "esp32-s3-zero", "esp32-s3-psram", "esp32-spk-n16r8",
  "esp32-s3-devkit-c1", "esp32-s3-cam-module", "esp32-wrover", "esp32-wroom", "esp32-mini",
  "wemos-lolin32-mini", "esp32-c3", "esp32-s2-psram", "esp32-c6", "wemos-d1-mini-esp32", "esp32-s2-wemos-mini", "viewe-uedx24320028e-wb-a", "viewe-uedx32480035e-wb-a",
  "viewe-uedx48480021-md80et", "viewe-uedx48480021-md80et-st7701s"];
const metadata=JSON.parse(fs.readFileSync(path.join(root,"scripts/runtime-board-catalog.json"),"utf8"));
const boardIds=JSON.parse(fs.readFileSync(path.join(root,"scripts/mainboard-svg-manifest.json"),"utf8"));
const app = fs.readFileSync(path.join(root, "web/app.js"), "utf8");
const html = fs.readFileSync(path.join(root, "web/index.html"), "utf8");
const boardContacts = JSON.parse(fs.readFileSync(path.join(root, "web/board-pin-contacts.json"), "utf8"));
const policy = await import("data:text/javascript;base64," + fs.readFileSync(path.join(root, "web/modules/board-pin-policy.js")).toString("base64"));
const onboard = await import("data:text/javascript;base64," + fs.readFileSync(path.join(root, "web/modules/onboard-boards.js")).toString("base64"));
const evaluate = expression => vm.runInNewContext(`(${expression})`, {}, {timeout: 1000});
const {suntonLogicSupported}=await import('../web/modules/sunton-capabilities.js');

function replaceFunction(source, name, replacement, indent = "") {
  const pattern = new RegExp(`^${indent}(?:export )?function ${name}\\([^]*?^${indent}\\}`, "m");
  if (!pattern.test(source)) throw new Error(`Missing specialization point: ${name}`);
  return source.replace(pattern, replacement);
}

// Large model rendering is fetched only when an optional BNO055 view is used.
const bnoViewer=await build({entryPoints:[path.join(root,"web/modules/bno055-tab.js")],bundle:true,format:"esm",minify:true,write:false,logLevel:"silent"});
const lazyBno=`export function createBno055Tab(options){
 const tab=document.querySelector('[data-tab="bno055"]'),panel=document.getElementById('tab-bno055');
 let real,pending,active=false,latest;
 function load(){if(real||pending)return;const url=new URL('/bno055-viewer.js',location.href).href;
  pending=import(url).then(module=>{real=module.createBno055Tab(options);if(latest)real.update(...latest);real.setActive(active);}).catch(error=>{pending=null;const notice=panel?.querySelector('[data-notice]');if(notice)notice.textContent='Orientation viewer: '+error.message;});}
 return {setActive(value){active=value;if(real)real.setActive(value);else if(value)load();},update(settings,locked){latest=[settings,locked];if(real){real.update(settings,locked);return;}
  let profiles=settings?.ui?.peripheralProfiles||{};if(typeof profiles==='string'){try{profiles=JSON.parse(profiles);}catch{profiles={};}}
  const available=profiles.sensors?.includes('bno055')&&!locked;if(tab)tab.hidden=!available;if(panel)panel.hidden=!available;if(available&&active)load();}};
}`;
function removePanel(source,id){
 const opening=new RegExp('<div\\b[^>]*\\bid="'+id+'"[^>]*>').exec(source);if(!opening)throw new Error('Missing panel '+id);
 const tags=/<\/?div\b[^>]*>/g;tags.lastIndex=opening.index+opening[0].length;let depth=1,tag;
 while((tag=tags.exec(source))){depth+=tag[0].startsWith('</')?-1:1;if(!depth)return source.slice(0,opening.index)+source.slice(tags.lastIndex);}
 throw new Error('Unbalanced panel '+id);
}
for (const [index, board] of boards.entries()) {
  const compactMultimedia=board.startsWith('esp32-2432')||board==='esp32-3248s035c';
  const chip = policy.boardChipFamily(board);
  const folder = path.join(output, "__boards", String(boardIds[metadata[board].asset.src.slice(1)]));
  fs.mkdirSync(folder, {recursive: true});
  let narrowedApp = app.replace(/^const PC_DESIGNER_RUNTIME = .*;$/m, "const PC_DESIGNER_RUNTIME = false;");
  if(board!=='esp32-3248s035c')narrowedApp=replaceFunction(narrowedApp,'applyConnectorOnlyControls','function applyConnectorOnlyControls() {}');
  if(compactMultimedia){
    // Settings is a relatively large JSON response. Finish it before requesting
    // status so a no-PSRAM device does not build both responses simultaneously.
    narrowedApp=narrowedApp.replace('Promise.allSettled([loadStatus(), loadSettings()])',
      'Promise.allSettled([loadSettings().then(() => loadStatus())])');
  }
  if(board==='esp32-3248s035c'){
    const allowed=onboard.ONBOARD_BOARDS[board].allowedProfiles;
    const names={AUDIO:'audio',AUDIO_IN:'audioIn',DISPLAY:'display',SENSOR:'sensor',INPUT:'input',STORAGE:'storage',COMMUNICATION:'communication',POWER:'power',CONTROL:'control',EXPANSION:'expansion'};
    narrowedApp=narrowedApp.replace(/^const PERIPHERAL_(\w+)_PROFILE_OPTIONS = (\[[^]*?^\]);/gm,(_,name,literal)=>
      `const PERIPHERAL_${name}_PROFILE_OPTIONS = ${JSON.stringify(evaluate(literal).filter(option=>allowed[names[name]]?.includes(option.value)))};`);
    narrowedApp=narrowedApp.replace('if (status.firmware?.touchscreen &&','if (!["playing","buffering"].includes(status.playback?.state) && status.firmware?.touchscreen &&');
  }
  if (!board.startsWith("viewe-") && !board.startsWith("esp32-2432") && board!=="esp32-3248s035c") {
    narrowedApp = narrowedApp.replace(/^  \{ value: "viewe-onboard-lcd",[^\n]*\n/m, "");
  }
  let maps = 0;
  narrowedApp = narrowedApp.replace(/^const (GPIO_BOARD_\w+) = (\{[^]*?^\});/gm, (_, name, literal) => {
    const data = evaluate(literal); ++maps;
    return `const ${name} = ${JSON.stringify(board in data ? {[board]: data[board]} : {})};`;
  });
  if (maps !== 5) throw new Error(`Board map coverage changed: found ${maps}`);
  narrowedApp = narrowedApp.replace(/const WS_STATUS_LED_BOARD_PROFILES = new Set\(\[.*?\]\);/,
    `const WS_STATUS_LED_BOARD_PROFILES = new Set(${JSON.stringify(metadata[board].defaults.statusLedType === "neopixel" ? [board] : [])});`);
  narrowedApp = replaceFunction(narrowedApp, "boardProfileChipFamily", `function boardProfileChipFamily() { return ${JSON.stringify(chip)}; }`);
  narrowedApp = replaceFunction(narrowedApp, "boardDefaultStatusLedPin", `function boardDefaultStatusLedPin() { return ${metadata[board].defaults.statusLedPin}; }`);
  narrowedApp = replaceFunction(narrowedApp, "detectGpioBoardProfile", `function detectGpioBoardProfile() { return ${JSON.stringify(board)}; }`);

  await build({
    entryPoints: [path.join(root, "web/app.js")], bundle: true, format: "esm", minify: true,
    outfile: path.join(folder, "app.js"), logLevel: "silent",
    plugins: [{name: "fixed-device-board", setup(plugin) {
      plugin.onLoad({filter: /\.js$/}, args => {
        const filename = path.basename(args.path);
        if(filename === "robot-tab.js" && board !== "miniexco-v2")return {contents:"export function createRobotTab(){return {setActive(){}}}",loader:"js"};
        if(compactMultimedia && filename==='camera-tab.js')return {contents:'export function createCameraTab(){return {setActive(){},update(){}}}',loader:'js'};
        if(compactMultimedia && filename==='microphones-tab.js')return {contents:'export function createMicrophonesTab(){return {setActive(){},update(){}}}',loader:'js'};
        if(compactMultimedia && filename==='motion-capture.js')return {contents:'export function createMotionCapture(){}',loader:'js'};
        if(compactMultimedia && filename==='logs-tab.js')return {contents:'export function createLogsTab(){return {setActive(){}}}',loader:'js'};
        if(compactMultimedia && filename==='bno055-tab.js')return {contents:lazyBno,loader:'js'};
        if(board==='esp32-3248s035c'&&filename==='logic-catalog.js'){
          const catalog=evaluate(fs.readFileSync(args.path,'utf8').split('export const LOGIC_CATALOG=')[1].trim().replace(/;$/,''));
          return {contents:'export const LOGIC_CATALOG='+JSON.stringify(Object.fromEntries(Object.entries(catalog).filter(([type])=>suntonLogicSupported(type))))+';',loader:'js'};
        }
        let source = fs.readFileSync(args.path, "utf8");
        if (args.path === path.join(root, "web/app.js")) source = narrowedApp;
        if (filename === "board-defaults.js") {
          source = `export const BOARD_DEFAULTS = ${JSON.stringify({[board]:metadata[board].defaults})};`;
        }
        if (filename === "onboard-boards.js") {
          source = `export const ONBOARD_BOARDS = ${JSON.stringify(onboard.ONBOARD_BOARDS[board] ? {[board]:onboard.ONBOARD_BOARDS[board]} : {})};`;
        }
        if (filename === "board-pin-policy.js") {
          source = replaceFunction(source, "boardChipFamily", `export function boardChipFamily() { return ${JSON.stringify(chip)}; }`);
          source = replaceFunction(source, "chipPins", `export function chipPins(chip, output = false) { return output ? ${JSON.stringify(policy.chipPins(chip, true, board))} : ${JSON.stringify(policy.chipPins(chip, false, board))}; }`);
        }
        if (filename === "board-contact-layout.js") {
          source = source.replace(/import boardPinContacts from '\.\.\/board-pin-contacts\.json'(?:\s+with\s*\{[^}]*\})?;/,
            `const boardPinContacts = ${JSON.stringify({[board]: boardContacts[board] || {}})};`);
        }
        if (filename === "configuration-gpio-tab.js") {
          const match = source.match(/^  function inferBoardAdcHint\([^]*?^  \}/m);
          if (!match) throw new Error("ADC hint specialization point missing");
          const hint = vm.runInNewContext(`${match[0]}; inferBoardAdcHint(${JSON.stringify(board)})`, {}, {timeout: 1000});
          source = replaceFunction(source, "inferBoardAdcHint", `  function inferBoardAdcHint() { return ${JSON.stringify(hint)}; }`, "  ");
          source = source.replace(/\{"esp32-c3":8[^{}]*\}/g, literal => {
            const data = evaluate(literal);
            return JSON.stringify(board in data ? {[board]: data[board]} : {});
          });
          source = source.replaceAll('"esp32-s3-super-mini"', JSON.stringify(board));
        }
        if (filename === "device-migration-tab.js") {
          source = source.replace(/  const boardOptions = (\{[^]*?^  \});/m, (_, literal) => {
            const data = evaluate(literal);
            const options = (data[chip] || []).filter(entry => entry[0] === board);
            return `  const boardOptions = ${JSON.stringify({[chip]: options})};`;
          });
        }
        if (filename === "peripheral-diagram-wiring.js") {
          source = source.replace(/^const (\w+) = (\{\s*"esp32-s3-zero"[^]*?^\});/gm, (_, name, literal) => {
            const data = evaluate(literal);
            return `const ${name} = ${JSON.stringify(board in data ? {[board]: data[board]} : {})};`;
          });
        }
        return {contents: source, loader: "js"};
      });
    }}],
  });
  // Narrow the board selector itself, including legacy/custom options that
  // are not part of this full-firmware build list.
  let narrowedHtml = html.replace(/(<select\b[^>]*id="gpioBoardSelector"[^>]*>)([^]*?)(<\/select>)/,
    (_, open, options, close) => open + options.replace(/<option\b[^>]*value="([^"]+)"[^>]*>[^<]*<\/option>/g,
      (tag, value) => value === board ? tag.replace(/ selected\b/, "").replace(">", " selected>") : "") + close);
  if (!board.startsWith("viewe-") && !board.startsWith("esp32-2432") && board!=="esp32-3248s035c") narrowedHtml = narrowedHtml.replace(/<option value="viewe-onboard-lcd">[^<]*<\/option>/g, "");
  if(compactMultimedia){
    for(const name of ['camera','microphones','robot','storage-internal','logs']){
      narrowedHtml=narrowedHtml.replace(new RegExp('<button\\b(?=[^>]*\\bdata-tab="'+name+'")[^>]*>[\\s\\S]*?<\\/button>','g'),'');
      narrowedHtml=removePanel(narrowedHtml,'tab-'+name);
    }
    fs.writeFileSync(path.join(folder,'bno055-viewer.js'),bnoViewer.outputFiles[0].contents);
  }
  if(board==='esp32-3248s035c'){
    narrowedHtml=narrowedHtml.replace(/(<select\b[^>]*id="peripheralDisplayProfile"[^>]*>)[^]*?(<\/select>)/,
      '$1<option value="viewe-onboard-lcd" selected>Onboard LCD + GT911 touch</option>$2');
    narrowedHtml=narrowedHtml.replace(/(<select\b[^>]*id="displayType"[^>]*>)[^]*?(<\/select>)/,
      '$1<option value="panel" selected>Sunton onboard LCD</option>$2');
    narrowedHtml=removePanel(narrowedHtml,'oledPreviewCard');
    narrowedHtml=removePanel(narrowedHtml,'wapeModeSection');
    narrowedHtml=narrowedHtml.replace(/<label\b[^>]*>[^]*?<\/label>/g,label=>
      /name="oled\.(driver|i2cAddress|width|height|sdaPin|sclPin|resetPin)"/.test(label)?'':label);
  }
  fs.writeFileSync(path.join(folder, "index.html"), narrowedHtml.replace(/>\s+</g, "><").replace(/\s{2,}/g, " "));
  const selector = narrowedHtml.match(/<select\b[^>]*id="gpioBoardSelector"[^>]*>([^]*?)<\/select>/)?.[1] || "";
  const choices = [...selector.matchAll(/<option\b[^>]*value="([^"]+)"/g)].map(match=>match[1]);
  if (choices.length !== 1 || choices[0] !== board) throw new Error(`Board selector is not fixed to ${board}`);
  const javascript = fs.readFileSync(path.join(folder, "app.js"), "utf8");
  for (const other of boards.filter(value => value !== board)) {
    if (javascript.includes(JSON.stringify(other)) || narrowedHtml.includes(`value="${other}"`)) {
      throw new Error(`Unexpected board reference ${other} in ${board}`);
    }
  }
  console.log(`[web-board] ${board}: ${javascript.length} JavaScript bytes; all peripheral definitions retained`);
}
