import {diagramRect} from './diagram-viewport.js';
import { isBoardGpio, isGroundSignal, isPositivePowerSignal, signalWireColor } from './peripheral-pin-model.js';
export const PERIPHERAL_DIAGRAM_LABEL_LAYOUTS_KEY = "__diagramLabelLayouts";
export const CUSTOM_PERIPHERAL_DIAGRAM_LABEL_VALUE = "__custom__";

export const STANDARD_PERIPHERAL_DIAGRAM_LABELS = [
  { value: "GND", label: "GND", badge: "var(--diagram-ground-color, #111827)", text: "var(--diagram-ground-text, #ffffff)" },
  { value: "VCC", label: "VCC", badge: "#dc2626", text: "#ffffff" },
  { value: "VIN", label: "VIN", badge: "#b91c1c", text: "#ffffff" },
  { value: "5V", label: "5V", badge: "#ef4444", text: "#ffffff" },
  { value: "12V", label: "12V", badge: "#b91c1c", text: "#ffffff" },
  { value: "24V", label: "24V", badge: "#991b1b", text: "#ffffff" },
  { value: "3V3", label: "3V3", badge: "#f97316", text: "#ffffff" },
  { value: "SDA", label: "SDA", badge: "#0284c7", text: "#ffffff" },
  { value: "SCL", label: "SCL", badge: "#0369a1", text: "#ffffff" },
  { value: "TX", label: "TX", badge: "#2563eb", text: "#ffffff" },
  { value: "RX", label: "RX", badge: "#0ea5e9", text: "#ffffff" },
  { value: "BCLK", label: "BCLK", badge: "#ca8a04", text: "#ffffff" },
  { value: "LRC", label: "LRC", badge: "#a16207", text: "#ffffff" },
  { value: "WS", label: "WS", badge: "#d97706", text: "#ffffff" },
  { value: "DIN", label: "DIN", badge: "#0891b2", text: "#ffffff" },
  { value: "DOUT", label: "DOUT", badge: "#0f766e", text: "#ffffff" },
  { value: "GAIN", label: "GAIN", badge: "#7c3aed", text: "#ffffff" },
  { value: "SD", label: "SD", badge: "#059669", text: "#ffffff" },
  { value: "MOSI", label: "MOSI", badge: "#16a34a", text: "#ffffff" },
  { value: "MISO", label: "MISO", badge: "#15803d", text: "#ffffff" },
  { value: "SCK", label: "SCK", badge: "#65a30d", text: "#ffffff" },
  { value: "CS", label: "CS", badge: "#059669", text: "#ffffff" },
  { value: "RST", label: "RST", badge: "#7c3aed", text: "#ffffff" },
  { value: "DC", label: "DC", badge: "#8b5cf6", text: "#ffffff" },
  { value: "BL", label: "BL", badge: "#a855f7", text: "#ffffff" },
  { value: "CLK", label: "CLK", badge: "#a16207", text: "#ffffff" },
  { value: "DATA", label: "DATA", badge: "#0ea5e9", text: "#ffffff" },
  { value: "SIG", label: "SIG", badge: "#0f766e", text: "#ffffff" },
  { value: "COM", label: "COM", badge: "#7c3aed", text: "#ffffff" },
  { value: "NC", label: "NC", badge: "#16a34a", text: "#ffffff" },
  { value: "NO", label: "NO", badge: "#0284c7", text: "#ffffff" },
  { value: "PWM", label: "PWM", badge: "#7c3aed", text: "#ffffff" },
  { value: "INT", label: "INT", badge: "#4f46e5", text: "#ffffff" },
  { value: "EN", label: "EN", badge: "#9333ea", text: "#ffffff" },
];

const STANDARD_PERIPHERAL_DIAGRAM_LABEL_MAP = new Map(
  STANDARD_PERIPHERAL_DIAGRAM_LABELS.map((entry) => [entry.value, entry]),
);

const PERIPHERAL_DIAGRAM_EDITOR_MIN_ZOOM = 0.1;
const PERIPHERAL_DIAGRAM_EDITOR_MAX_ZOOM = 10;
const PERIPHERAL_DIAGRAM_EDITOR_ZOOM_STEP = 0.1;

function isPlainObject(value) {
  return Boolean(value) && typeof value === "object" && !Array.isArray(value);
}

function ensureLayoutStore(state) {
  if (!isPlainObject(state.peripheralDiagramPositions)) {
    state.peripheralDiagramPositions = {};
  }
  if (!isPlainObject(state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_LABEL_LAYOUTS_KEY])) {
    state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_LABEL_LAYOUTS_KEY] = {};
  }
  return state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_LABEL_LAYOUTS_KEY];
}

export function normalizePeripheralDiagramLabelName(value) {
  return String(value || "")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 24);
}

export function peripheralDiagramLabelId(value) {
  const normalized = normalizePeripheralDiagramLabelName(value).toUpperCase();
  return normalized.replace(/[^A-Z0-9]+/g, "_").replace(/^_+|_+$/g, "") || "LABEL";
}

export function peripheralDiagramBoardNodeId(boardProfile) {
  return `BOARD_${peripheralDiagramLabelId(boardProfile || "ESP")}`;
}

export function peripheralDiagramBoardLabelEntryId({ pin, label, side = "left", lane = 0, index = 0 }) {
  if (isBoardGpio({pin})) {
    return `BOARD_GPIO_${Number(pin)}`;
  }
  return `BOARD_${String(side).toUpperCase()}_${Number(lane) || 0}_${Number(index) || 0}_${peripheralDiagramLabelId(label)}`;
}

export function peripheralDiagramBoardLabelDefaultLayout({ side = "left", lane = 0, index = 0, count = 1 }) {
  const normalizedCount = Math.max(Number(count) || 1, 1);
  const normalizedIndex = Math.min(Math.max(Number(index) || 0, 0), normalizedCount - 1);
  const yFactor = normalizedCount === 1
    ? 0
    : -0.48 + ((normalizedIndex / Math.max(normalizedCount - 1, 1)) * 0.96);
  const laneOffset = (Number(lane) || 0) * 0.48;
  const xFactor = String(side) === "right"
    ? 0.72 + laneOffset
    : -0.72 - laneOffset;
  return {
    xFactor,
    yFactor,
    rotation: 0,
    contactSide: String(side),
    contactLane: Number(lane) || 0,
  };
}

function normalizeStoredNumber(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function normalizeStoredEntry(id, value, order = 0) {
  if (!isPlainObject(value)) {
    return null;
  }

  const label = normalizePeripheralDiagramLabelName(value.label || value.name || id);
  if (!label) {
    return null;
  }

  return {
    id: String(value.id || id),
    label,
    xFactor: normalizeStoredNumber(value.xFactor, 0),
    yFactor: normalizeStoredNumber(value.yFactor, 0),
    rotation: normalizeStoredNumber(value.rotation, 0),
    order: normalizeStoredNumber(value.order, order),
    isCustom: Boolean(value.isCustom),
    isRemoved: Boolean(value.isRemoved),
    coordinateSpace: String(value.coordinateSpace || "node"),
  };
}

export function readPeripheralDiagramNodeLabels(state, nodeId) {
  const store = ensureLayoutStore(state);
  const nodeLayouts = isPlainObject(store[nodeId]) ? store[nodeId] : null;
  if (!nodeLayouts) {
    return [];
  }

  return Object.entries(nodeLayouts)
    .map(([id, value], index) => normalizeStoredEntry(id, value, index))
    .filter(Boolean)
    .sort((left, right) => {
      if (left.order !== right.order) {
        return left.order - right.order;
      }
      return left.label.localeCompare(right.label);
    });
}

export function writePeripheralDiagramNodeLabels(state, nodeId, labels) {
  const store = ensureLayoutStore(state);
  const entries = Array.isArray(labels)
    ? labels
      .map((label, index) => normalizeStoredEntry(label?.id || `label-${index + 1}`, {
        ...label,
        order: index,
      }, index))
      .filter(Boolean)
    : [];

  if (!entries.length) {
    delete store[nodeId];
    return;
  }

  store[nodeId] = {};
  entries.forEach((entry, index) => {
    store[nodeId][entry.id] = {
      label: entry.label,
      xFactor: entry.xFactor,
      yFactor: entry.yFactor,
      rotation: entry.rotation,
      order: index,
      isCustom: Boolean(entry.isCustom),
      isRemoved: Boolean(entry.isRemoved),
      coordinateSpace: String(entry.coordinateSpace || "visual"),
    };
  });
}

export function readPeripheralDiagramLabelLayout(state, nodeId, labelId) {
  return readPeripheralDiagramNodeLabels(state, nodeId).find((entry) => entry.id === labelId) || null;
}

export function writePeripheralDiagramLabelLayout(state, nodeId, labelId, layout) {
  const current = readPeripheralDiagramNodeLabels(state, nodeId);
  const existing = current.find((entry) => entry.id === labelId);
  const next = existing
    ? current.map((entry) => (entry.id === labelId ? { ...entry, ...layout } : entry))
    : [...current, {
      id: labelId,
      label: normalizePeripheralDiagramLabelName(layout.label || labelId),
      xFactor: normalizeStoredNumber(layout.xFactor, 0),
      yFactor: normalizeStoredNumber(layout.yFactor, 0),
      rotation: normalizeStoredNumber(layout.rotation, 0),
      order: current.length,
      isCustom: Boolean(layout.isCustom),
      coordinateSpace: String(layout.coordinateSpace || "visual"),
    }];
  writePeripheralDiagramNodeLabels(state, nodeId, next);
}

  function normalizeQuarterTurns(deltaDegrees) {
    if (!Number.isFinite(deltaDegrees)) {
      return 0;
    }
    const quarterTurns = Math.round(deltaDegrees / 90) % 4;
    return quarterTurns < 0 ? quarterTurns + 4 : quarterTurns;
  }

  function rotateVisualPointClockwise(dx, dy, turns) {
    let nextX = dx;
    let nextY = dy;
    for (let index = 0; index < turns; index += 1) {
      const rotatedX = -nextY;
      const rotatedY = nextX;
      nextX = rotatedX;
      nextY = rotatedY;
    }
    return { x: nextX, y: nextY };
  }

  export function rotatePeripheralDiagramNodeLabels(state, nodeId, {
    deltaDegrees = 90,
    visualWidth = 1,
    visualHeight = 1,
  } = {}) {
    const turns = normalizeQuarterTurns(deltaDegrees);
    if (!turns) {
      return;
    }

    const width = Math.max(Number(visualWidth) || 0, 1);
    const height = Math.max(Number(visualHeight) || 0, 1);
    const labels = readPeripheralDiagramNodeLabels(state, nodeId);
    if (!labels.length) {
      return;
    }

    const nextLabels = labels.map((entry) => {
      const rotated = rotateVisualPointClockwise(
        Number(entry.xFactor || 0) * width,
        Number(entry.yFactor || 0) * height,
        turns,
      );
      return {
        ...entry,
        xFactor: rotated.x / width,
        yFactor: rotated.y / height,
        rotation: (Number(entry.rotation || 0) + (turns * 90)) % 360,
        coordinateSpace: String(entry.coordinateSpace || "visual"),
      };
    });

    writePeripheralDiagramNodeLabels(state, nodeId, nextLabels);
  }

function rectHasArea(rect) {
  return Boolean(rect?.width) && Boolean(rect?.height);
}

function convertLayoutToRect(layout, fromRect, toRect) {
  if (!rectHasArea(fromRect) || !rectHasArea(toRect)) {
    return { ...layout };
  }
  return {
    ...layout,
    xFactor: (Number(layout.xFactor || 0) * fromRect.width) / toRect.width,
    yFactor: (Number(layout.yFactor || 0) * fromRect.height) / toRect.height,
  };
}

export function peripheralDiagramLabelPalette(label, fallback = null) {
  if (!isGroundSignal(label) && !isPositivePowerSignal(label) && !/^GPIO\d+$/i.test(String(label))) {
    return { badge: signalWireColor(label), text: '#ffffff' };
  }
  const standard = STANDARD_PERIPHERAL_DIAGRAM_LABEL_MAP.get(peripheralDiagramLabelId(label));
  if (standard) {
    return { badge: standard.badge, text: standard.text };
  }

  const key = peripheralDiagramLabelId(label);
  if (/^GPIO\d+$/.test(key) || ["GPIO", "TX_GPIO1", "RX_GPIO3", "TX_GPIO16", "RX_GPIO17"].includes(key)) {
    return { badge: "#16a34a", text: "#ffffff" };
  }
  if (["GND", "GROUND"].includes(key)) {
    return { badge: "var(--diagram-ground-color, #111827)", text: "var(--diagram-ground-text, #ffffff)" };
  }
  if (["VCC", "VIN", "VBUS", "5V"].includes(key)) {
    return { badge: "#dc2626", text: "#ffffff" };
  }
  if (["3V3", "3_3V", "3VO", "PWR"].includes(key)) {
    return { badge: "#ea580c", text: "#ffffff" };
  }
  if (["SDA", "RX", "DIN", "DATA", "DOUT", "NO"].includes(key)) {
    return { badge: "#0284c7", text: "#ffffff" };
  }
  if (["SCL", "TX", "BCLK", "WS", "CLK", "SCK"].includes(key)) {
    return { badge: "#ca8a04", text: "#ffffff" };
  }
  if (["CS", "RST", "RESET", "BL", "DC", "INT", "EN", "NC"].includes(key)) {
    return { badge: "#16a34a", text: "#ffffff" };
  }
  if (["PWM", "SIG", "COM", "OUT", "IN", "A", "B"].includes(key)) {
    return { badge: "#7c3aed", text: "#ffffff" };
  }
  return fallback || { badge: "#0f766e", text: "#ffffff" };
}

function selectedPresetValue(label) {
  const key = peripheralDiagramLabelId(label);
  return STANDARD_PERIPHERAL_DIAGRAM_LABEL_MAP.has(key) ? key : CUSTOM_PERIPHERAL_DIAGRAM_LABEL_VALUE;
}

function createCustomLabelId(existingIds) {
  let index = existingIds.size + 1;
  while (existingIds.has(`CUSTOM_${index}`)) {
    index += 1;
  }
  return `CUSTOM_${index}`;
}

function mergeDefaultLabel(defaultLabel, savedLabel, index) {
  return {
    id: String(defaultLabel.id || peripheralDiagramLabelId(defaultLabel.label)),
    label: normalizePeripheralDiagramLabelName(savedLabel?.label || defaultLabel.label),
    xFactor: savedLabel?.xFactor ?? defaultLabel.xFactor ?? null,
    yFactor: savedLabel?.yFactor ?? defaultLabel.yFactor ?? null,
    rotation: savedLabel?.rotation ?? defaultLabel.rotation ?? 0,
    order: savedLabel?.order ?? defaultLabel.order ?? index,
    isCustom: Boolean(savedLabel?.isCustom || defaultLabel.isCustom),
    coordinateSpace: savedLabel?.coordinateSpace || defaultLabel.coordinateSpace || "visual",
  };
}

export function resolvePeripheralDiagramNodeLabels(state, nodeId, defaultLabels = []) {
  // Discard the old null->GPIO0 identity collision for board rails. Retain real
  // GPIO0 labels and every other saved/custom layout.
  const savedLabels = readPeripheralDiagramNodeLabels(state, nodeId).filter(entry =>
    !(String(nodeId).startsWith('BOARD_') && entry.id === 'BOARD_GPIO_0'
      && (isGroundSignal(entry.label) || isPositivePowerSignal(entry.label))));
  const savedById = new Map(savedLabels.map((entry) => [entry.id, entry]));
  const defaults = defaultLabels
    .map((entry, index) => {
      const id = String(entry.id || peripheralDiagramLabelId(entry.label));
      const saved = savedById.get(id)
        || (entry.legacyIds || []).map((legacyId) => savedById.get(String(legacyId))).find(Boolean);
      return { ...mergeDefaultLabel(entry, saved, index), legacyIds: entry.legacyIds || [] };
    })
    .filter((entry) => {
      const saved = savedById.get(entry.id)
        || entry.legacyIds.map((legacyId) => savedById.get(String(legacyId))).find(Boolean);
      return !saved?.isRemoved;
    });
  const defaultIds = new Set(defaults.flatMap((entry) => [entry.id, ...entry.legacyIds.map(String)]));
  const extras = savedLabels
    .filter((entry) => !entry.isRemoved && !defaultIds.has(entry.id))
    .map((entry, index) => ({
      ...entry,
      order: entry.order ?? (defaults.length + index),
      isCustom: true,
    }));

  return [...defaults, ...extras].sort((left, right) => {
    if (left.order !== right.order) {
      return left.order - right.order;
    }
    return left.label.localeCompare(right.label);
  });
}

function modalLabelDefaultLayout(index, count) {
  const row = Math.floor(index / 2);
  const column = index % 2 === 0 ? 1 : -1;
  const verticalCenter = (count - 1) / 4;
  return {
    xFactor: 0.28 * column,
    yFactor: (row - verticalCenter) * 0.22,
    rotation: 0,
  };
}

function labelCenterPixels(layout, metrics) {
  return {
    x: metrics.centerX + (layout.xFactor * metrics.visualWidth),
    y: metrics.centerY + (layout.yFactor * metrics.visualHeight),
  };
}

function clampValue(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function labelLayoutFromPixels(center, rotation, metrics) {
  return {
    xFactor: metrics.visualWidth > 0 ? ((center.x - metrics.centerX) / metrics.visualWidth) : 0,
    yFactor: metrics.visualHeight > 0 ? ((center.y - metrics.centerY) / metrics.visualHeight) : 0,
    rotation: Number(rotation || 0),
  };
}

function optionMarkup(entry) {
  return `<option value="${entry.value}">${entry.label}</option>`;
}

function escapeHtml(value) {
  return String(value || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

export function createPeripheralDiagramLabelEditorModule({
  state,
  elements,
  renderPeripheralDiagram,
  savePeripheralDiagramPositions,
  buildEditablePeripheralLabels,
}) {
  const dragState = {
    labelId: null,
    pointerId: null,
    offsetX: 0,
    offsetY: 0,
  };

  const panState = {
    pointerId: null,
    startX: 0,
    startY: 0,
    originX: 0,
    originY: 0,
  };

  let currentNode = null;
  let currentLabelsDraft = [];
  let currentRemovedDefaultIds = new Set();
  let activeLabelId = null;
  let currentZoom = 1;
  let currentPanX = 0;
  let currentPanY = 0;
  let needsLayoutSanitization = false;

  function clampZoom(value) {
    return Math.min(PERIPHERAL_DIAGRAM_EDITOR_MAX_ZOOM, Math.max(PERIPHERAL_DIAGRAM_EDITOR_MIN_ZOOM, value));
  }

  function visualSurfaceElement() {
    return elements.peripheralDiagramLabelEditorVisual?.querySelector("img, .peripheral-diagram-editor-visual-fallback") || null;
  }

  function mainDiagramRects(nodeId) {
    const nodeElement = document.querySelector(`[data-node-id="${nodeId}"]`);
    const visual = nodeElement?.querySelector(".peripheral-diagram-node-visual");
    const visualSurface = visual?.firstElementChild || visual;
    return {
      nodeRect: diagramRect(nodeElement) || null,
      visualRect: diagramRect(visualSurface) || diagramRect(visual) || null,
    };
  }

  function applyZoomStyles() {
    const zoom = clampZoom(currentZoom);
    currentZoom = zoom;
    if (elements.peripheralDiagramLabelEditorVisual) {
      elements.peripheralDiagramLabelEditorVisual.style.transform = `translate(${currentPanX}px, ${currentPanY}px) scale(${zoom})`;
      elements.peripheralDiagramLabelEditorVisual.style.transformOrigin = "center center";
    }
    if (elements.peripheralDiagramLabelEditorLabels) {
      elements.peripheralDiagramLabelEditorLabels.style.transform = `translate(${currentPanX}px, ${currentPanY}px) scale(${zoom})`;
      elements.peripheralDiagramLabelEditorLabels.style.transformOrigin = "center center";
    }
    if (elements.peripheralDiagramLabelEditorStage) {
      elements.peripheralDiagramLabelEditorStage.dataset.zoom = zoom.toFixed(2);
      elements.peripheralDiagramLabelEditorStage.classList.toggle("is-panning", panState.pointerId != null);
    }
  }

  function screenToUnscaled(screenPoint, metrics) {
    return {
      x: metrics.stageCenterX + ((screenPoint.x - currentPanX - metrics.stageCenterX) / metrics.zoom),
      y: metrics.stageCenterY + ((screenPoint.y - currentPanY - metrics.stageCenterY) / metrics.zoom),
    };
  }

  function previewMetrics() {
    const stage = elements.peripheralDiagramLabelEditorStage;
    const target = visualSurfaceElement();
    if (!stage || !target) {
      return null;
    }

    const stageRect = stage.getBoundingClientRect();
    const visualWidth = target.offsetWidth || target.clientWidth;
    const visualHeight = target.offsetHeight || target.clientHeight;
    const visualLeft = target.offsetLeft;
    const visualTop = target.offsetTop;

    if (!stageRect.width || !stageRect.height) {
      return null;
    }
    const zoom = clampZoom(currentZoom);
    const stageCenterX = stageRect.width / 2;
    const stageCenterY = stageRect.height / 2;

    if (!visualWidth || !visualHeight) {
      return null;
    }

    const visualCenterX = visualLeft + (visualWidth / 2);
    const visualCenterY = visualTop + (visualHeight / 2);
    return {
      stageRect,
      zoom,
      stageCenterX,
      stageCenterY,
      visualWidth,
      visualHeight,
      centerX: visualCenterX,
      centerY: visualCenterY,
    };
  }

  function sanitizeCurrentLayouts(metrics) {
    const labelPadding = 60;
    let changed = false;
    currentLabelsDraft = currentLabelsDraft.map((label) => {
      const center = labelCenterPixels(label, metrics);
      const clampedCenter = {
        x: clampValue(center.x, labelPadding, metrics.stageRect.width - labelPadding),
        y: clampValue(center.y, labelPadding, metrics.stageRect.height - labelPadding),
      };
      if (clampedCenter.x === center.x && clampedCenter.y === center.y) {
        return label;
      }
      changed = true;
      return {
        ...label,
        ...labelLayoutFromPixels(clampedCenter, label.rotation, metrics),
        coordinateSpace: "visual",
      };
    });
    needsLayoutSanitization = false;
    return changed;
  }

  function sanitizeLabelLayout(label, metrics) {
    if (!label || !metrics) {
      return label;
    }
    const labelPadding = 60;
    const center = labelCenterPixels(label, metrics);
    const clampedCenter = {
      x: clampValue(center.x, labelPadding, metrics.stageRect.width - labelPadding),
      y: clampValue(center.y, labelPadding, metrics.stageRect.height - labelPadding),
    };
    if (clampedCenter.x === center.x && clampedCenter.y === center.y) {
      return label;
    }
    return {
      ...label,
      ...labelLayoutFromPixels(clampedCenter, label.rotation, metrics),
      coordinateSpace: "visual",
    };
  }

  function freezeCurrentLayouts(metrics) {
    if (!metrics) {
      return;
    }
    currentLabelsDraft = currentLabelsDraft.map((label) => {
      const center = labelCenterPixels(label, metrics);
      return {
        ...label,
        ...labelLayoutFromPixels(center, label.rotation, metrics),
        coordinateSpace: "visual",
      };
    });
  }

  function defaultLabels() {
    return currentNode ? buildEditablePeripheralLabels(currentNode) : [];
  }

  function activeLabel() {
    return currentLabelsDraft.find((entry) => entry.id === activeLabelId) || null;
  }

  function ensureDraftLayouts() {
    currentLabelsDraft = currentLabelsDraft.map((entry, index, array) => {
      if (entry.xFactor != null && entry.yFactor != null) {
        return {
          ...entry,
          coordinateSpace: entry.coordinateSpace || "visual",
        };
      }
      return {
        ...entry,
        ...modalLabelDefaultLayout(index, array.length),
        coordinateSpace: entry.coordinateSpace || "visual",
      };
    });
  }

  function normalizeDraftCoordinateSpaces() {
    if (!currentNode) {
      return;
    }
    const { nodeRect, visualRect } = mainDiagramRects(currentNode.id);
    currentLabelsDraft = currentLabelsDraft.map((entry) => {
      if (entry.coordinateSpace === "visual") {
        return entry;
      }
      return {
        ...convertLayoutToRect(entry, nodeRect, visualRect),
        coordinateSpace: "visual",
      };
    });
  }

  function draftNeedsLayoutSanitization() {
    return currentLabelsDraft.some((entry) => (
      entry.xFactor == null
      || entry.yFactor == null
      || !Number.isFinite(Number(entry.xFactor))
      || !Number.isFinite(Number(entry.yFactor))
      || String(entry.coordinateSpace || "visual") !== "visual"
    ));
  }

  function renderInspector() {
    const editor = elements.peripheralDiagramLabelEditorInspector;
    const hint = elements.peripheralDiagramLabelEditorHint;
    const preset = elements.peripheralDiagramLabelEditorPreset;
    const name = elements.peripheralDiagramLabelEditorName;
    const removeButton = elements.peripheralDiagramLabelEditorRemove;
    if (!editor || !hint || !preset || !name || !removeButton) {
      return;
    }

    const entry = activeLabel();
    if (!entry) {
      editor.hidden = true;
      hint.hidden = false;
      return;
    }

    hint.hidden = true;
    editor.hidden = false;
    preset.value = selectedPresetValue(entry.label);
    name.value = entry.label;
    removeButton.disabled = false;
  }

  function syncActiveLabelSelection() {
    elements.peripheralDiagramLabelEditorLabels
      ?.querySelectorAll(".peripheral-diagram-editor-label[data-editor-label]")
      .forEach((labelElement) => {
        const selected = String(labelElement.dataset.editorLabel || "") === activeLabelId;
        labelElement.classList.toggle("is-active-edit", selected);
        labelElement.setAttribute("aria-pressed", String(selected));
      });
  }

  function renderEditorLabels() {
    const stage = elements.peripheralDiagramLabelEditorStage;
    const layer = elements.peripheralDiagramLabelEditorLabels;
    if (!stage || !layer || !currentNode) {
      return;
    }

    ensureDraftLayouts();
    applyZoomStyles();
    const metrics = previewMetrics();
    if (!metrics) {
      setTimeout(() => {
        if (currentNode && elements.peripheralDiagramLabelEditorModal?.open) {
          renderEditorLabels();
        }
      }, 60);
      return;
    }

    if (needsLayoutSanitization) {
      sanitizeCurrentLayouts(metrics);
    }

    layer.innerHTML = currentLabelsDraft.map((label) => {
      const center = labelCenterPixels(label, metrics);
      const transform = `translate(-50%, -50%) rotate(${Number(label.rotation || 0)}deg)`;
      const palette = peripheralDiagramLabelPalette(label.label);
      const activeClass = activeLabelId === label.id ? " is-active-edit" : "";
      const safeLabel = escapeHtml(label.label);
      return `
        <button
          type="button"
          class="peripheral-diagram-editor-label${activeClass}"
          data-editor-label="${label.id}"
          style="left:${center.x}px; top:${center.y}px; transform:${transform}; background:${palette.badge}; color:${palette.text};"
          aria-label="${safeLabel} label"
          aria-pressed="${activeLabelId === label.id}"
          title="Drag to reposition ${safeLabel}"
        >
          <span class="peripheral-diagram-editor-label-text">${safeLabel}</span>
          <span class="peripheral-diagram-editor-label-actions">
            <span class="peripheral-diagram-editor-label-edit" data-editor-label-edit="${label.id}" aria-hidden="true">✎</span>
            <span class="peripheral-diagram-editor-label-rotate" data-editor-label-rotate="${label.id}" aria-hidden="true">↻</span>
          </span>
        </button>
      `;
    }).join("");

    renderInspector();
  }

  function renderEditorVisual() {
    if (!elements.peripheralDiagramLabelEditorVisual || !elements.peripheralDiagramLabelEditorTitle || !elements.peripheralDiagramLabelEditorSubtitle || !currentNode) {
      return;
    }

    const usesAsset = Boolean(currentNode.src);
    const title = escapeHtml(currentNode.title || currentNode.label);
    elements.peripheralDiagramLabelEditorTitle.textContent = currentNode.title || currentNode.label;
    elements.peripheralDiagramLabelEditorSubtitle.textContent = "Drag labels into place, rotate them, edit names, and use the mouse wheel to zoom the canvas.";
    const visualTransform = String(currentNode.visualTransform || "").trim();
    const visualStyle = visualTransform ? ` style="transform:${escapeHtml(visualTransform)};"` : "";
    elements.peripheralDiagramLabelEditorVisual.innerHTML = usesAsset
      ? `<img src="${currentNode.src}" alt="${title} module" draggable="false"${visualStyle}>`
      : `
          <div class="peripheral-diagram-editor-visual-fallback" aria-label="${title} placeholder">
            <div class="peripheral-diagram-editor-visual-fallback-title">${title}</div>
            <div class="peripheral-diagram-editor-visual-fallback-pins">${(Array.isArray(currentNode.pins) ? currentNode.pins : []).map((pin) => `<span>${escapeHtml(pin)}</span>`).join("")}</div>
          </div>
        `;
  }

  function renderEditor() {
    renderEditorVisual();
    setTimeout(renderEditorLabels, 0);
  }

  async function saveCurrentDrafts() {
    if (!currentNode) {
      return;
    }
    ensureDraftLayouts();
    freezeCurrentLayouts(previewMetrics());
    const savedEntries = currentLabelsDraft.map((entry) => ({
      ...entry,
      coordinateSpace: "visual",
    }));
    const removedDefaults = defaultLabels()
      .filter((entry) => currentRemovedDefaultIds.has(entry.id) && !currentLabelsDraft.some((draft) => draft.id === entry.id))
      .map((entry, index) => ({
        id: entry.id,
        label: entry.label,
        xFactor: Number(entry.xFactor || 0),
        yFactor: Number(entry.yFactor || 0),
        rotation: Number(entry.rotation || 0),
        order: currentLabelsDraft.length + index,
        isCustom: false,
        isRemoved: true,
        coordinateSpace: String(entry.coordinateSpace || "visual"),
      }));
    writePeripheralDiagramNodeLabels(state, currentNode.id, savedEntries.concat(removedDefaults));
    await savePeripheralDiagramPositions({ immediate: true });
  }

  async function closeEditor() {
    await saveCurrentDrafts();
    elements.peripheralDiagramLabelEditorModal?.close();
    renderPeripheralDiagram();
  }

  function open(node) {
    if (!node || !elements.peripheralDiagramLabelEditorModal?.showModal) {
      return;
    }
    currentNode = node;
    currentRemovedDefaultIds = new Set(
      readPeripheralDiagramNodeLabels(state, currentNode.id)
        .filter((entry) => entry.isRemoved)
        .map((entry) => entry.id),
    );
    currentLabelsDraft = resolvePeripheralDiagramNodeLabels(state, currentNode.id, defaultLabels()).map((entry) => ({ ...entry }));
    needsLayoutSanitization = draftNeedsLayoutSanitization();
    normalizeDraftCoordinateSpaces();
    activeLabelId = currentLabelsDraft[0]?.id || null;
    currentZoom = 1;
    currentPanX = 0;
    currentPanY = 0;
    elements.peripheralDiagramLabelEditorModal.showModal();
    setTimeout(renderEditor, 0);
  }

  function labelElementFromEventTarget(target) {
    return target instanceof Element ? target.closest(".peripheral-diagram-editor-label[data-editor-label]") : null;
  }

  function updateActiveLabel(mutator) {
    if (!activeLabelId) {
      return;
    }
    currentLabelsDraft = currentLabelsDraft.map((entry) => (entry.id === activeLabelId ? mutator(entry) : entry));
    renderEditorLabels();
  }

  function addLabel() {
    ensureDraftLayouts();
    const metrics = previewMetrics();
    freezeCurrentLayouts(metrics);
    const existingIds = new Set(currentLabelsDraft.map((entry) => entry.id));
    const removedDefault = defaultLabels().find((entry) => currentRemovedDefaultIds.has(entry.id) && !existingIds.has(entry.id));
    const nextPreset = STANDARD_PERIPHERAL_DIAGRAM_LABELS.find((entry) => !currentLabelsDraft.some((label) => peripheralDiagramLabelId(label.label) === entry.value))
      || STANDARD_PERIPHERAL_DIAGRAM_LABELS.find((entry) => entry.value === "SIG");
    const nextEntry = sanitizeLabelLayout(removedDefault
      ? {
        ...removedDefault,
        order: currentLabelsDraft.length,
        isCustom: Boolean(removedDefault.isCustom),
        isRemoved: false,
        coordinateSpace: String(removedDefault.coordinateSpace || "visual"),
      }
      : {
        id: createCustomLabelId(existingIds),
        label: nextPreset?.label || "Custom",
        ...modalLabelDefaultLayout(currentLabelsDraft.length, currentLabelsDraft.length + 1),
        rotation: 0,
        order: currentLabelsDraft.length,
        isCustom: true,
        isRemoved: false,
        coordinateSpace: "visual",
      }, metrics);
    currentRemovedDefaultIds.delete(nextEntry.id);
    currentLabelsDraft = [...currentLabelsDraft, nextEntry];
    activeLabelId = nextEntry.id;
    renderEditorLabels();
  }

  function handleStagePointerDown(event) {
    if (!currentNode) {
      return;
    }

    if (event.button === 1) {
      panState.pointerId = event.pointerId;
      panState.startX = event.clientX;
      panState.startY = event.clientY;
      panState.originX = currentPanX;
      panState.originY = currentPanY;
      applyZoomStyles();
      event.preventDefault();
      return;
    }

    if (event.button !== 0) {
      return;
    }

    const editButton = event.target instanceof Element ? event.target.closest("[data-editor-label-edit]") : null;
    if (editButton) {
      activeLabelId = String(editButton.getAttribute("data-editor-label-edit") || "");
      renderEditorLabels();
      event.preventDefault();
      return;
    }

    const rotateButton = event.target instanceof Element ? event.target.closest("[data-editor-label-rotate]") : null;
    if (rotateButton) {
      return;
    }

    const labelElement = labelElementFromEventTarget(event.target);
    if (!labelElement) {
      return;
    }

    const metrics = previewMetrics();
    if (!metrics) {
      return;
    }

    const labelRect = labelElement.getBoundingClientRect();
    dragState.labelId = String(labelElement.dataset.editorLabel || "");
    activeLabelId = dragState.labelId;
    syncActiveLabelSelection();
    dragState.pointerId = event.pointerId;
    dragState.offsetX = event.clientX - (labelRect.left + (labelRect.width / 2));
    dragState.offsetY = event.clientY - (labelRect.top + (labelRect.height / 2));
    labelElement.classList.add("is-dragging");
    renderInspector();
    event.preventDefault();
  }

  function handleStagePointerMove(event) {
    if (panState.pointerId === event.pointerId) {
      currentPanX = panState.originX + (event.clientX - panState.startX);
      currentPanY = panState.originY + (event.clientY - panState.startY);
      applyZoomStyles();
      event.preventDefault();
      return;
    }

    if (!currentNode || dragState.pointerId !== event.pointerId || !dragState.labelId) {
      return;
    }

    const metrics = previewMetrics();
    if (!metrics) {
      return;
    }

    const center = {
      x: Math.min(Math.max(18, event.clientX - metrics.stageRect.left - dragState.offsetX), metrics.stageRect.width - 18),
      y: Math.min(Math.max(18, event.clientY - metrics.stageRect.top - dragState.offsetY), metrics.stageRect.height - 18),
    };
    const unscaledCenter = screenToUnscaled(center, metrics);
    currentLabelsDraft = currentLabelsDraft.map((entry) => (
      entry.id === dragState.labelId
        ? { ...entry, ...labelLayoutFromPixels(unscaledCenter, entry.rotation, metrics), coordinateSpace: "visual" }
        : entry
    ));
    renderEditorLabels();
  }

  function handleStagePointerUp(event) {
    if (panState.pointerId === event.pointerId) {
      panState.pointerId = null;
      applyZoomStyles();
    }

    if (dragState.pointerId !== event.pointerId || !dragState.labelId) {
      return;
    }

    const dragging = elements.peripheralDiagramLabelEditorLabels?.querySelector(`[data-editor-label="${dragState.labelId}"]`);
    dragging?.classList.remove("is-dragging");
    dragState.labelId = null;
    dragState.pointerId = null;
  }

  function handleStageClick(event) {
    if (!currentNode) {
      return;
    }

    const rotateButton = event.target instanceof Element ? event.target.closest("[data-editor-label-rotate]") : null;
    if (rotateButton) {
      event.preventDefault();
      const labelId = String(rotateButton.getAttribute("data-editor-label-rotate") || "");
      activeLabelId = labelId;
      updateActiveLabel((entry) => ({
        ...entry,
        rotation: ((Number(entry.rotation || 0) + 90) % 360),
        coordinateSpace: "visual",
      }));
      return;
    }

    const labelElement = labelElementFromEventTarget(event.target);
    if (labelElement) {
      activeLabelId = String(labelElement.dataset.editorLabel || "");
      syncActiveLabelSelection();
      renderInspector();
    }
  }

  function handlePresetChange() {
    const entry = activeLabel();
    const presetValue = String(elements.peripheralDiagramLabelEditorPreset?.value || CUSTOM_PERIPHERAL_DIAGRAM_LABEL_VALUE);
    if (!entry) {
      return;
    }
    if (presetValue === CUSTOM_PERIPHERAL_DIAGRAM_LABEL_VALUE) {
      renderInspector();
      return;
    }
    const preset = STANDARD_PERIPHERAL_DIAGRAM_LABEL_MAP.get(presetValue);
    if (!preset) {
      return;
    }
    updateActiveLabel((item) => ({ ...item, label: preset.label }));
  }

  function handleNameInput() {
    const value = normalizePeripheralDiagramLabelName(elements.peripheralDiagramLabelEditorName?.value);
    if (!value) {
      return;
    }
    updateActiveLabel((entry) => ({ ...entry, label: value }));
  }

  function handleRemove() {
    const entry = activeLabel();
    if (!entry) {
      return;
    }
    if (!entry.isCustom) {
      currentRemovedDefaultIds.add(entry.id);
    }
    currentLabelsDraft = currentLabelsDraft.filter((label) => label.id !== entry.id).map((label, index) => ({ ...label, order: index }));
    activeLabelId = currentLabelsDraft[0]?.id || null;
    renderEditorLabels();
  }

  function handleStageWheel(event) {
    if (!currentNode) {
      return;
    }
    event.preventDefault();
    const direction = event.deltaY < 0 ? 1 : -1;
    currentZoom = clampZoom(currentZoom + (direction * PERIPHERAL_DIAGRAM_EDITOR_ZOOM_STEP));
    renderEditor();
  }

  function setup() {
    if (elements.peripheralDiagramLabelEditorPreset) {
      elements.peripheralDiagramLabelEditorPreset.innerHTML = [
        `<option value="${CUSTOM_PERIPHERAL_DIAGRAM_LABEL_VALUE}">Custom</option>`,
        ...STANDARD_PERIPHERAL_DIAGRAM_LABELS.map(optionMarkup),
      ].join("");
    }
    elements.peripheralDiagramLabelEditorClose?.addEventListener("click", closeEditor);
    elements.peripheralDiagramLabelEditorAdd?.addEventListener("click", addLabel);
    elements.peripheralDiagramLabelEditorPreset?.addEventListener("change", handlePresetChange);
    elements.peripheralDiagramLabelEditorName?.addEventListener("input", handleNameInput);
    elements.peripheralDiagramLabelEditorRemove?.addEventListener("click", handleRemove);
    elements.peripheralDiagramLabelEditorModal?.addEventListener("cancel", (event) => {
      event.preventDefault();
      closeEditor();
    });
    elements.peripheralDiagramLabelEditorStage?.addEventListener("pointerdown", handleStagePointerDown);
    elements.peripheralDiagramLabelEditorStage?.addEventListener("click", handleStageClick);
    elements.peripheralDiagramLabelEditorStage?.addEventListener("wheel", handleStageWheel, { passive: false });
    document.addEventListener("pointermove", handleStagePointerMove);
    document.addEventListener("pointerup", handleStagePointerUp);
    document.addEventListener("pointercancel", handleStagePointerUp);
  }

  setup();

  return {
    open,
  };
}
