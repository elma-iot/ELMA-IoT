import {diagramRect,canvasClientPoint} from './diagram-viewport.js';
import {voltageDividerMaximum} from './voltage-divider.js';
import {
  peripheralDiagramLabelPalette,
  peripheralDiagramLabelId,
  peripheralDiagramBoardNodeId,
  peripheralDiagramBoardLabelEntryId,
  peripheralDiagramBoardLabelDefaultLayout,
  resolvePeripheralDiagramNodeLabels,
} from "./peripheral-diagram-label-editor.js";

const PERIPHERAL_DIAGRAM_WIRE_CURVES_KEY = "__wireCurves";
const PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY = "__customLabelWires";
const PERIPHERAL_DIAGRAM_HIDDEN_CONNECTIONS_KEY = "__hiddenConnections";

function normalizeSignalLabel(label) {
  return String(label || "")
    .replace(/^OLED\s+/i, "")
    .replace(/^I2S\s+/i, "")
    .replace(/^SD\s+/i, "")
    .replace(/^TX\s*\/\s*/i, "TX ")
    .replace(/^RX\s*\/\s*/i, "RX ")
    .trim();
}

function signalKey(label) {
  return normalizeSignalLabel(label).toUpperCase();
}

function labelReferenceKey(nodeId, labelKey) {
  return `${String(nodeId || "")}:${String(labelKey || "")}`;
}

function customLabelConnectionKey(connection) {
  return `${labelReferenceKey(connection?.fromNodeId, connection?.fromLabelKey)}->${labelReferenceKey(connection?.toNodeId, connection?.toLabelKey)}`;
}

function isLimitSwitchNode(node) {
  return String(node?.groupKey || "") === "input"
    && String(node?.profileValue || "none").trim().toLowerCase().includes("limit-switch");
}

function limitSwitchSourceValueForBoardEntry(entry) {
  const label = String(entry?.label || entry?.boardLabel || "").trim();
  const key = signalKey(label);
  if (key === "GND" || key === "GROUND") {
    return "GND";
  }
  if (["VCC", "VIN", "PWR", "VBUS", "5V", "12V", "3V3", "3.3V", "3VO"].includes(key)) {
    return "VCC";
  }
  return null;
}

function limitSwitchBoardLabelForSource(node, sourceValue) {
  return String(sourceValue || "").trim().toUpperCase() === "VCC"
    ? (powerRailLabelForNode(node, "VCC") || "3V3")
    : "GND";
}

function isPositivePowerSignal(label) {
  const key = signalKey(label);
  return ["VCC", "VIN", "PWR", "VBUS", "5V", "12V", "3V3", "3.3V", "3VO"].includes(key)
    || key.startsWith("VCC ")
    || key.startsWith("VIN ")
    || key.startsWith("5V ")
    || key.startsWith("12V ")
    || key.startsWith("3V3 ")
    || key.startsWith("3.3V ");
}

function isGroundSignal(label) {
  const key = signalKey(label);
  return key === "GND" || key === "GROUND" || key.startsWith("GND ") || key.startsWith("GROUND ");
}

function powerRailLabelForNode(node, label) {
  const key = signalKey(label);
  if (key === "GND" || key === "GROUND" || key.startsWith("GND ") || key.startsWith("GROUND ")) {
    return "GND";
  }
  if (key === "12V" || key.startsWith("12V ")) {
    return "12V";
  }
  if (key === "5V" || key === "VIN" || key === "VBUS" || key.startsWith("5V ") || key.startsWith("VIN ")) {
    return "5V";
  }
  if (key === "3V3" || key === "3.3V" || key === "3VO" || key.startsWith("3V3 ") || key.startsWith("3.3V ")) {
    return "3V3";
  }
  if (!["VCC", "PWR"].includes(key) && !key.startsWith("VCC ")) {
    return null;
  }
  if (String(node?.groupKey || "") === "power") {
    return "5V";
  }
  return ["audio", "control"].includes(String(node?.groupKey || "")) ? "5V" : "3V3";
}

function classifyWireColor(connection) {
  const signal = signalKey(connection.signalLabel);
  const board = signalKey(connection.boardLabel);

  if (isGroundSignal(signal) || board === "GND") {
    return { stroke: "#111827", glow: "rgba(17, 24, 39, 0.18)", badge: "#111827", text: "#ffffff" };
  }
  if (board === "5V" || signal === "5V" || signal === "VIN" || signal === "VBUS" || signal.startsWith("5V ") || signal.startsWith("VIN ")) {
    return { stroke: "#dc2626", glow: "rgba(220, 38, 38, 0.18)", badge: "#dc2626", text: "#ffffff" };
  }
  if (board === "3V3" || signal === "3V3" || signal === "3.3V" || signal === "VCC" || signal === "PWR" || signal.startsWith("VCC ") || signal.startsWith("3V3 ") || signal.startsWith("3.3V ")) {
    return { stroke: "#ea580c", glow: "rgba(234, 88, 12, 0.18)", badge: "#ea580c", text: "#ffffff" };
  }
  if (["SDA", "MOSI", "DIN", "DOUT", "DATA", "DQ", "RX", "NO"].includes(signal)) {
    return { stroke: "#0284c7", glow: "rgba(2, 132, 199, 0.18)", badge: "#0284c7", text: "#ffffff" };
  }
  if (["SCL", "SCK", "CLK", "BCLK", "WS", "PCLK", "TX"].includes(signal)) {
    return { stroke: "#ca8a04", glow: "rgba(202, 138, 4, 0.18)", badge: "#ca8a04", text: "#ffffff" };
  }
  if (["CS", "RST", "RESET", "DC", "BL", "INT", "EN", "STBY", "TRIG", "TRIGGER", "CTRL", "CMD", "NC"].includes(signal)) {
    return { stroke: "#16a34a", glow: "rgba(22, 163, 74, 0.18)", badge: "#16a34a", text: "#ffffff" };
  }
  if (["PWM", "PWM1", "PWM2", "OUT", "IN", "SIG", "COM", "GPIO", "A", "B", "SW", "BUS"].includes(signal)) {
    return { stroke: "#7c3aed", glow: "rgba(124, 58, 237, 0.18)", badge: "#7c3aed", text: "#ffffff" };
  }
  return { stroke: "#0f766e", glow: "rgba(15, 118, 110, 0.18)", badge: "#0f766e", text: "#ffffff" };
}

function colorWithAlpha(color, alpha = 0.18) {
  const value = String(color || "").trim();
  const match = /^#([0-9a-f]{6})$/i.exec(value);
  if (!match) {
    return `rgba(15, 118, 110, ${alpha})`;
  }
  const hex = match[1];
  const red = Number.parseInt(hex.slice(0, 2), 16);
  const green = Number.parseInt(hex.slice(2, 4), 16);
  const blue = Number.parseInt(hex.slice(4, 6), 16);
  return `rgba(${red}, ${green}, ${blue}, ${alpha})`;
}

function normalizeWirePalette(palette) {
  const stroke = String(palette?.stroke || palette?.badge || "#0f766e");
  return {
    stroke,
    glow: String(palette?.glow || colorWithAlpha(stroke)),
    badge: String(palette?.badge || stroke),
    text: String(palette?.text || "#ffffff"),
  };
}

function boardTargetKey(connection) {
  return connection.type === "gpio"
    ? `gpio:${connection.pin}`
    : `rail:${String(connection.boardLabel || "").toUpperCase()}`;
}

function boardSideForNode(nodeRect, boardRect) {
  const nodeCenterX = nodeRect.left + (nodeRect.width / 2);
  const nodeCenterY = nodeRect.top + (nodeRect.height / 2);
  const boardCenterX = boardRect.left + (boardRect.width / 2);
  const boardCenterY = boardRect.top + (boardRect.height / 2);
  const deltaX = nodeCenterX - boardCenterX;
  const deltaY = nodeCenterY - boardCenterY;

  if (Math.abs(deltaX) >= Math.abs(deltaY)) {
    return deltaX < 0 ? "left" : "right";
  }
  return deltaY < 0 ? "top" : "bottom";
}

function nodeAnchorForConnection(nodeRect, boardRect, connectionIndex, connectionCount) {
  const side = boardSideForNode(nodeRect, boardRect);
  const spreadIndex = connectionIndex + 1;
  const spreadCount = Math.max(connectionCount + 1, 2);

  if (side === "left") {
    return {
      x: nodeRect.left + nodeRect.width,
      y: nodeRect.top + ((nodeRect.height / spreadCount) * spreadIndex),
      side,
    };
  }
  if (side === "right") {
    return {
      x: nodeRect.left,
      y: nodeRect.top + ((nodeRect.height / spreadCount) * spreadIndex),
      side,
    };
  }
  if (side === "top") {
    return {
      x: nodeRect.left + ((nodeRect.width / spreadCount) * spreadIndex),
      y: nodeRect.top + nodeRect.height,
      side,
    };
  }
  return {
    x: nodeRect.left + ((nodeRect.width / spreadCount) * spreadIndex),
    y: nodeRect.top,
    side,
  };
}

function createSvgElement(tagName) {
  return document.createElementNS("http://www.w3.org/2000/svg", tagName);
}

function anchorDetourPoint(anchor, ownerRect, target, margin = 26) {
  const ownerLeft = Number(ownerRect?.left || 0);
  const ownerTop = Number(ownerRect?.top || 0);
  const ownerRight = ownerLeft + Number(ownerRect?.width || 0);
  const ownerBottom = ownerTop + Number(ownerRect?.height || 0);
  const ownerCenterX = ownerLeft + ((ownerRight - ownerLeft) / 2);
  const ownerCenterY = ownerTop + ((ownerBottom - ownerTop) / 2);

  if (anchor.side === "top" || anchor.side === "bottom") {
    return {
      x: target.x < ownerCenterX ? ownerLeft - margin : ownerRight + margin,
      y: anchor.y,
    };
  }

  return {
    x: anchor.x,
    y: target.y < ownerCenterY ? ownerTop - margin : ownerBottom + margin,
  };
}

function connectionPath(nodeAnchor, boardAnchor, nodeOwnerRect, boardOwnerRect) {
  const startDetour = anchorDetourPoint(nodeAnchor, nodeOwnerRect, boardAnchor);
  const endDetour = anchorDetourPoint(boardAnchor, boardOwnerRect, nodeAnchor);
  const points = [
    `M ${nodeAnchor.x} ${nodeAnchor.y}`,
    `L ${startDetour.x} ${startDetour.y}`,
  ];

  if (Math.abs(startDetour.x - endDetour.x) > 0.5 || Math.abs(startDetour.y - endDetour.y) > 0.5) {
    const intermediate = (nodeAnchor.side === "top" || nodeAnchor.side === "bottom")
      ? { x: endDetour.x, y: startDetour.y }
      : { x: startDetour.x, y: endDetour.y };
    if (Math.abs(intermediate.x - startDetour.x) > 0.5 || Math.abs(intermediate.y - startDetour.y) > 0.5) {
      points.push(`L ${intermediate.x} ${intermediate.y}`);
    }
  }

  points.push(`L ${endDetour.x} ${endDetour.y}`);
  points.push(`L ${boardAnchor.x} ${boardAnchor.y}`);
  return points.join(" ");
}

function defaultConnectionControlPoint(nodeAnchor, startDetour, endDetour) {
  const controlPoint = (nodeAnchor.side === "top" || nodeAnchor.side === "bottom")
    ? { x: endDetour.x, y: startDetour.y }
    : { x: startDetour.x, y: endDetour.y };

  if (Math.abs(controlPoint.x - startDetour.x) <= 0.5 && Math.abs(controlPoint.y - startDetour.y) <= 0.5) {
    return {
      x: (startDetour.x + endDetour.x) / 2,
      y: (startDetour.y + endDetour.y) / 2,
    };
  }

  return controlPoint;
}

function quadraticPointAt(startPoint, controlPoint, endPoint, t) {
  const inverse = 1 - t;
  return {
    x: (inverse * inverse * startPoint.x) + (2 * inverse * t * controlPoint.x) + (t * t * endPoint.x),
    y: (inverse * inverse * startPoint.y) + (2 * inverse * t * controlPoint.y) + (t * t * endPoint.y),
  };
}

function controlPointFromHandlePoint(startPoint, endPoint, handlePoint) {
  return {
    x: (2 * handlePoint.x) - ((startPoint.x + endPoint.x) / 2),
    y: (2 * handlePoint.y) - ((startPoint.y + endPoint.y) / 2),
  };
}

function anchorSideFromVector(deltaX, deltaY) {
  if (Math.abs(deltaX) >= Math.abs(deltaY)) {
    return deltaX < 0 ? "left" : "right";
  }
  return deltaY < 0 ? "top" : "bottom";
}

function anchorLeadPoint(anchor, fallbackPoint, distance = 26) {
  const tangentX = Number(anchor?.tangentX);
  const tangentY = Number(anchor?.tangentY);
  if (Number.isFinite(tangentX) && Number.isFinite(tangentY)) {
    return {
      x: Number(anchor.x || 0) + (tangentX * distance),
      y: Number(anchor.y || 0) + (tangentY * distance),
    };
  }
  return {
    x: Number(fallbackPoint?.x || anchor?.x || 0),
    y: Number(fallbackPoint?.y || anchor?.y || 0),
  };
}

function cloneCurvePoints(points) {
  return Array.isArray(points)
    ? points
      .map((point) => ({
        x: Number(point?.x),
        y: Number(point?.y),
      }))
      .filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y))
    : [];
}

function dedupeAdjacentPoints(points, threshold = 0.75) {
  const deduped = [];
  points.forEach((point) => {
    const previous = deduped[deduped.length - 1];
    if (!previous || Math.hypot(previous.x - point.x, previous.y - point.y) > threshold) {
      deduped.push(point);
    }
  });
  return deduped;
}

function pointDistance(left, right) {
  return Math.hypot(Number(right?.x || 0) - Number(left?.x || 0), Number(right?.y || 0) - Number(left?.y || 0));
}

function pointOnSegmentDistance(point, start, end) {
  const dx = Number(end?.x || 0) - Number(start?.x || 0);
  const dy = Number(end?.y || 0) - Number(start?.y || 0);
  const lengthSquared = (dx * dx) + (dy * dy);
  if (lengthSquared <= 0.001) {
    return pointDistance(point, start);
  }
  const t = Math.max(0, Math.min(1, (((point.x - start.x) * dx) + ((point.y - start.y) * dy)) / lengthSquared));
  return pointDistance(point, {
    x: start.x + (dx * t),
    y: start.y + (dy * t),
  });
}

function insertPointIntoRoute(routePoints, point, start, end) {
  if (!routePoints.length) {
    return { points: [point], index: 0 };
  }

  const anchors = [start, ...routePoints, end];
  let bestIndex = 0;
  let bestDistance = Number.POSITIVE_INFINITY;
  for (let index = 0; index < anchors.length - 1; index += 1) {
    const distance = pointOnSegmentDistance(point, anchors[index], anchors[index + 1]);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = index;
    }
  }

  const nextPoints = [...routePoints];
  nextPoints.splice(bestIndex, 0, point);
  return { points: nextPoints, index: bestIndex };
}

function smoothBezierPath(points) {
  if (points.length < 2) {
    return "";
  }
  if (points.length === 2) {
    return `M ${points[0].x} ${points[0].y} L ${points[1].x} ${points[1].y}`;
  }

  const commands = [`M ${points[0].x} ${points[0].y}`];
  for (let index = 0; index < points.length - 1; index += 1) {
    const previous = points[index - 1] || points[index];
    const current = points[index];
    const next = points[index + 1];
    const afterNext = points[index + 2] || next;
    const control1 = {
      x: current.x + ((next.x - previous.x) / 6),
      y: current.y + ((next.y - previous.y) / 6),
    };
    const control2 = {
      x: next.x - ((afterNext.x - current.x) / 6),
      y: next.y - ((afterNext.y - current.y) / 6),
    };
    commands.push(`C ${control1.x} ${control1.y} ${control2.x} ${control2.y} ${next.x} ${next.y}`);
  }
  return commands.join(" ");
}

function defaultRoutePoints(nodeAnchor, boardAnchor, nodeOwnerRect, boardOwnerRect) {
  const startLead = anchorLeadPoint(
    nodeAnchor,
    anchorDetourPoint(nodeAnchor, nodeOwnerRect, boardAnchor),
  );
  const endLead = anchorLeadPoint(
    boardAnchor,
    anchorDetourPoint(boardAnchor, boardOwnerRect, nodeAnchor),
  );
  const controlPoint = defaultConnectionControlPoint(nodeAnchor, startLead, endLead);
  return dedupeAdjacentPoints([
    startLead,
    controlPoint,
    endLead,
  ]);
}

function connectionGeometry(nodeAnchor, boardAnchor, nodeOwnerRect, boardOwnerRect, manualCurvePoints = []) {
  const routePoints = cloneCurvePoints(manualCurvePoints);
  const defaultPoints = defaultRoutePoints(nodeAnchor, boardAnchor, nodeOwnerRect, boardOwnerRect);
  const points = dedupeAdjacentPoints([
    { x: nodeAnchor.x, y: nodeAnchor.y },
    ...(routePoints.length ? routePoints : defaultPoints),
    { x: boardAnchor.x, y: boardAnchor.y },
  ]);

  return {
    path: smoothBezierPath(points),
    controlPoint: routePoints[0] || points[Math.min(2, points.length - 2)] || boardAnchor,
    handlePoint: routePoints[0] || points[Math.min(2, points.length - 2)] || boardAnchor,
    handlePoints: routePoints.length ? routePoints : defaultPoints,
    manualCurvePoints: routePoints,
  };
}

function selectedOptionText(element, fallback) {
  return String(element?.selectedOptions?.[0]?.textContent || fallback || "").trim();
}

const BOARD_ANCHOR_CALIBRATIONS = {
  "esp32-s3-zero": {
    topInsetRatio: 0.065,
    bottomInsetRatio: 0.06,
    outerOffset: 5,
    extraLaneGap: 11,
  },
};

function boardAnchorCalibration(boardProfile) {
  return {
    topInsetRatio: 0.055,
    bottomInsetRatio: 0.055,
    outerOffset: 8,
    extraLaneGap: 12,
    ...(BOARD_ANCHOR_CALIBRATIONS[boardProfile] || {}),
  };
}

function setActiveConnection(overlay, activeGroup) {
  const groups = overlay.querySelectorAll(".peripheral-diagram-wire-connection");
  const hovering = Boolean(activeGroup);
  overlay.classList.toggle("is-connection-hovering", hovering);
  groups.forEach((group) => {
    group.classList.toggle("is-active", group === activeGroup);
  });
}

function ensureLabelLayer(stage) {
  if (!stage) {
    return null;
  }

  let layer = stage.querySelector(".peripheral-diagram-label-layer");
  if (!layer) {
    layer = document.createElement("div");
    layer.className = "peripheral-diagram-label-layer";
    stage.appendChild(layer);
  }
  return layer;
}

function relativeRectForElement(element, stageRect) {
  const rect = diagramRect(element);
  if (!rect?.width || !rect?.height) {
    return null;
  }
  return {
    left: rect.left - stageRect.left,
    top: rect.top - stageRect.top,
    width: rect.width,
    height: rect.height,
  };
}

function nodeVisualRect(nodeElement, stageRect) {
  const visual = nodeElement?.querySelector(".peripheral-diagram-node-visual");
  const visualSurface = visual?.firstElementChild || visual;
  return relativeRectForElement(visualSurface, stageRect) || relativeRectForElement(visual, stageRect) || relativeRectForElement(nodeElement, stageRect);
}

function signalLabelStorageKey(nodeId, signalLabel) {
  return `${nodeId}:${signalKey(signalLabel)}`;
}

function connectionStorageKey(connection) {
  return `${String(connection.nodeId || "node")}:${signalKey(connection.signalLabel)}:${boardTargetKey(connection)}`;
}

function ensureWireCurveStore(state) {
  state.peripheralDiagramPositions ||= {};
  const current = state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_WIRE_CURVES_KEY];
  if (!current || typeof current !== "object" || Array.isArray(current)) {
    state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_WIRE_CURVES_KEY] = {};
  }
  return state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_WIRE_CURVES_KEY];
}

function clearWireCurveStore(state) {
  if (!state.peripheralDiagramPositions || typeof state.peripheralDiagramPositions !== "object") {
    state.peripheralDiagramPositions = {};
    return false;
  }
  const current = state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_WIRE_CURVES_KEY];
  if (!current || typeof current !== "object" || Array.isArray(current) || !Object.keys(current).length) {
    delete state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_WIRE_CURVES_KEY];
    return false;
  }
  delete state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_WIRE_CURVES_KEY];
  return true;
}

function ensureCustomLabelWireStore(state) {
  state.peripheralDiagramPositions ||= {};
  const current = state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY];
  if (!Array.isArray(current)) {
    state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY] = [];
  }
  return state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY];
}

function readCustomLabelConnections(state) {
  return ensureCustomLabelWireStore(state)
    .map((entry) => ({
      fromNodeId: String(entry?.fromNodeId || ""),
      fromLabelKey: String(entry?.fromLabelKey || ""),
      toNodeId: String(entry?.toNodeId || ""),
      toLabelKey: String(entry?.toLabelKey || ""),
    }))
    .filter((entry) => entry.fromNodeId && entry.fromLabelKey && entry.toNodeId && entry.toLabelKey);
}

function writeCustomLabelConnections(state, connections) {
  const normalized = Array.isArray(connections)
    ? connections
      .map((entry) => ({
        fromNodeId: String(entry?.fromNodeId || ""),
        fromLabelKey: String(entry?.fromLabelKey || ""),
        toNodeId: String(entry?.toNodeId || ""),
        toLabelKey: String(entry?.toLabelKey || ""),
      }))
      .filter((entry) => entry.fromNodeId && entry.fromLabelKey && entry.toNodeId && entry.toLabelKey)
    : [];
  if (!normalized.length) {
    delete state.peripheralDiagramPositions?.[PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY];
    return;
  }
  state.peripheralDiagramPositions ||= {};
  state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY] = normalized;
}

function upsertCustomLabelConnection(state, connection) {
  const current = readCustomLabelConnections(state);
  const key = customLabelConnectionKey(connection);
  if (current.some((entry) => customLabelConnectionKey(entry) === key)) {
    return false;
  }
  writeCustomLabelConnections(state, [...current, connection]);
  return true;
}

function clearCustomLabelWireStore(state) {
  const current = state.peripheralDiagramPositions?.[PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY];
  if (!Array.isArray(current) || !current.length) {
    delete state.peripheralDiagramPositions?.[PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY];
    return false;
  }
  delete state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_CUSTOM_LABEL_WIRES_KEY];
  return true;
}

function ensureHiddenConnectionStore(state) {
  state.peripheralDiagramPositions ||= {};
  const current = state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_HIDDEN_CONNECTIONS_KEY];
  if (!current || typeof current !== "object" || Array.isArray(current)) {
    state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_HIDDEN_CONNECTIONS_KEY] = {};
  }
  return state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_HIDDEN_CONNECTIONS_KEY];
}

function isConnectionHidden(state, connectionKey) {
  return Boolean(ensureHiddenConnectionStore(state)[String(connectionKey || "")]);
}

function hideConnection(state, connectionKey) {
  const key = String(connectionKey || "").trim();
  if (!key) {
    return false;
  }
  const store = ensureHiddenConnectionStore(state);
  if (store[key]) {
    return false;
  }
  store[key] = true;
  return true;
}

function clearHiddenConnectionStore(state) {
  const current = state.peripheralDiagramPositions?.[PERIPHERAL_DIAGRAM_HIDDEN_CONNECTIONS_KEY];
  if (!current || typeof current !== "object" || Array.isArray(current) || !Object.keys(current).length) {
    delete state.peripheralDiagramPositions?.[PERIPHERAL_DIAGRAM_HIDDEN_CONNECTIONS_KEY];
    return false;
  }
  delete state.peripheralDiagramPositions[PERIPHERAL_DIAGRAM_HIDDEN_CONNECTIONS_KEY];
  return true;
}

function clearStoredWireCurveByKey(state, connectionKey) {
  const key = String(connectionKey || "").trim();
  if (!key) {
    return false;
  }
  const store = ensureWireCurveStore(state);
  if (!Object.prototype.hasOwnProperty.call(store, key)) {
    return false;
  }
  delete store[key];
  return true;
}

function readStoredWireCurve(state, key, stageRect) {
  const entry = ensureWireCurveStore(state)[key];
  if (!entry || !stageRect?.width || !stageRect?.height) {
    return [];
  }
  if (Array.isArray(entry.points)) {
    return entry.points
      .map((point) => {
        const xFactor = Number(point?.xFactor);
        const yFactor = Number(point?.yFactor);
        if (!Number.isFinite(xFactor) || !Number.isFinite(yFactor)) {
          return null;
        }
        return {
          x: clampValue(xFactor * stageRect.width, 0, stageRect.width),
          y: clampValue(yFactor * stageRect.height, 0, stageRect.height),
        };
      })
      .filter(Boolean);
  }
  const xFactor = Number(entry.xFactor);
  const yFactor = Number(entry.yFactor);
  if (!Number.isFinite(xFactor) || !Number.isFinite(yFactor)) {
    return [];
  }
  return [{
    x: clampValue(xFactor * stageRect.width, 0, stageRect.width),
    y: clampValue(yFactor * stageRect.height, 0, stageRect.height),
  }];
}

function writeStoredWireCurve(state, key, points, stageRect) {
  const normalized = cloneCurvePoints(points);
  if (!normalized.length || !stageRect?.width || !stageRect?.height) {
    delete ensureWireCurveStore(state)[key];
    return;
  }
  ensureWireCurveStore(state)[key] = {
    points: normalized.map((point) => ({
      xFactor: clampValue(point.x / stageRect.width, 0, 1),
      yFactor: clampValue(point.y / stageRect.height, 0, 1),
    })),
  };
}

function signalLabelDefaultLayout(nodeRect, anchor, signalLabel) {
  const label = normalizeSignalLabel(signalLabel) || "SIG";
  const width = Math.max(28, (label.length * 6.6) + 14);
  const sideOffset = anchor.side === "left"
    ? 8
    : (anchor.side === "right"
      ? -(width + 8)
      : -(width / 2));
  const verticalOffset = anchor.side === "top" ? 8 : -26;
  const centerX = anchor.x + sideOffset + (width / 2);
  const centerY = anchor.y + verticalOffset + 9;
  return {
    xFactor: nodeRect.width > 0 ? ((centerX - (nodeRect.left + (nodeRect.width / 2))) / nodeRect.width) : 0,
    yFactor: nodeRect.height > 0 ? ((centerY - (nodeRect.top + (nodeRect.height / 2))) / nodeRect.height) : 0,
    rotation: 0,
  };
}

function floatingLabelDefaultLayout(nodeRect, index, count) {
  const verticalCenter = (count - 1) / 2;
  return {
    xFactor: 0,
    yFactor: ((index - verticalCenter) * 0.18) - 0.32,
    rotation: 0,
  };
}

function clampValue(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function convertLayoutToVisualSpace(layout, sourceRect, visualRect) {
  if (!sourceRect?.width || !sourceRect?.height || !visualRect?.width || !visualRect?.height) {
    return {
      xFactor: Number(layout?.xFactor || 0),
      yFactor: Number(layout?.yFactor || 0),
      rotation: Number(layout?.rotation || 0),
    };
  }
  return {
    xFactor: (Number(layout?.xFactor || 0) * sourceRect.width) / visualRect.width,
    yFactor: (Number(layout?.yFactor || 0) * sourceRect.height) / visualRect.height,
    rotation: Number(layout?.rotation || 0),
  };
}

function renderSignalLabels(layer, labelEntries) {
  if (!layer) {
    return;
  }

  layer.innerHTML = "";
  const stage = layer.parentElement;
  const stageWidth = stage?.clientWidth || 0;
  const stageHeight = stage?.clientHeight || 0;
  const labelPadding = 36;
  labelEntries.forEach((entry) => {
    const rawCenterX = entry.nodeRect.left + (entry.nodeRect.width / 2) + (entry.layout.xFactor * entry.nodeRect.width);
    const rawCenterY = entry.nodeRect.top + (entry.nodeRect.height / 2) + (entry.layout.yFactor * entry.nodeRect.height);
    const centerX = stageWidth > 0 ? clampValue(rawCenterX, labelPadding, stageWidth - labelPadding) : rawCenterX;
    const centerY = stageHeight > 0 ? clampValue(rawCenterY, labelPadding, stageHeight - labelPadding) : rawCenterY;
    const element = document.createElement("div");
    element.className = "peripheral-diagram-floating-label";
    element.dataset.nodeId = String(entry.nodeId || "");
    element.dataset.labelKey = String(entry.labelKey || entry.label || "");
    element.style.left = `${centerX}px`;
    element.style.top = `${centerY}px`;
    element.style.transform = `translate(-50%, -50%) rotate(${Number(entry.layout.rotation || 0)}deg)`;

    const pill = document.createElement("span");
    pill.className = "peripheral-diagram-floating-label-pill";
    pill.textContent = entry.label;
    pill.style.background = entry.palette.badge;
    pill.style.color = entry.palette.text;
    element.appendChild(pill);
    if(entry.dividerMaximum){const maximum=document.createElement('small');maximum.className='voltage-divider-pin-maximum';maximum.textContent=`MAX: ${entry.dividerMaximum.output.toFixed(2)} V`;maximum.classList.toggle('voltage-divider-unsafe',entry.dividerMaximum.unsafe);element.append(maximum);}
    layer.appendChild(element);
  });
}

function renderedLabelRects(layer, stageRect) {
  const rects = new Map();
  if (!layer || !stageRect) {
    return rects;
  }
  layer.querySelectorAll(".peripheral-diagram-floating-label[data-node-id][data-label-key]").forEach((element) => {
    const rect = diagramRect(element);
    if (!rect.width || !rect.height) {
      return;
    }
    const nodeId = String(element.dataset.nodeId || "");
    const labelKey = String(element.dataset.labelKey || "");
    if (!nodeId || !labelKey) {
      return;
    }
    rects.set(`${nodeId}:${labelKey}`, {
      left: rect.left - stageRect.left,
      top: rect.top - stageRect.top,
      width: rect.width,
      height: rect.height,
    });
  });
  return rects;
}

function labelDisplayCenter(entry, stageWidth, stageHeight, labelPadding = 36) {
  const rawCenterX = entry.nodeRect.left + (entry.nodeRect.width / 2) + (entry.layout.xFactor * entry.nodeRect.width);
  const rawCenterY = entry.nodeRect.top + (entry.nodeRect.height / 2) + (entry.layout.yFactor * entry.nodeRect.height);
  return {
    x: stageWidth > 0 ? clampValue(rawCenterX, labelPadding, stageWidth - labelPadding) : rawCenterX,
    y: stageHeight > 0 ? clampValue(rawCenterY, labelPadding, stageHeight - labelPadding) : rawCenterY,
  };
}

function floatingLabelSize(label) {
  const normalized = normalizeSignalLabel(label) || "SIG";
  return {
    width: Math.max(28, (normalized.length * 6.6) + 30),
    height: 24,
  };
}

function labelAnchorAwayFromOwner(entry, ownerRect, stageWidth, stageHeight, renderedRect = null) {
  const center = renderedRect
    ? {
      x: renderedRect.left + (renderedRect.width / 2),
      y: renderedRect.top + (renderedRect.height / 2),
    }
    : labelDisplayCenter(entry, stageWidth, stageHeight);
  const size = floatingLabelSize(entry.label);
  const halfWidth = Number(size.width || 0) / 2;
  const halfHeight = Number(size.height || 0) / 2;
  const ownerCenterX = Number(ownerRect?.left || 0) + (Number(ownerRect?.width || 0) / 2);
  const ownerCenterY = Number(ownerRect?.top || 0) + (Number(ownerRect?.height || 0) / 2);
  const deltaX = center.x - ownerCenterX;
  const deltaY = center.y - ownerCenterY;

  if (Math.abs(deltaX) < 0.001 && Math.abs(deltaY) < 0.001) {
    return { x: center.x + halfWidth, y: center.y, side: "right", tangentX: 1, tangentY: 0 };
  }

  const rotationRadians = (Number(entry?.layout?.rotation || 0) * Math.PI) / 180;
  const axisX = Math.cos(rotationRadians);
  const axisY = Math.sin(rotationRadians);
  const projection = (deltaX * axisX) + (deltaY * axisY);
  const direction = projection >= 0 ? 1 : -1;
  const edgeWorldX = center.x + (axisX * halfWidth * direction);
  const edgeWorldY = center.y + (axisY * halfWidth * direction);
  const tangentX = axisX * direction;
  const tangentY = axisY * direction;
  const side = anchorSideFromVector(tangentX, tangentY);

  return {
    x: edgeWorldX,
    y: edgeWorldY,
    side,
    tangentX,
    tangentY,
  };
}

function boardLabelAnchor(entry, boardRect, stageWidth, stageHeight, renderedRect = null) {
  const center = renderedRect
    ? {
      x: renderedRect.left + (renderedRect.width / 2),
      y: renderedRect.top + (renderedRect.height / 2),
    }
    : labelDisplayCenter(entry, stageWidth, stageHeight);
  const size = floatingLabelSize(entry.label);
  const halfWidth = Number(size.width || 0) / 2;
  const halfHeight = Number(size.height || 0) / 2;
  const boardCenterX = Number(boardRect?.left || 0) + (Number(boardRect?.width || 0) / 2);
  const boardCenterY = Number(boardRect?.top || 0) + (Number(boardRect?.height || 0) / 2);
  const deltaX = center.x - boardCenterX;
  const deltaY = center.y - boardCenterY;

  if (Math.abs(deltaX) >= Math.abs(deltaY)) {
    const direction = deltaX >= 0 ? 1 : -1;
    return {
      x: renderedRect
        ? (direction > 0 ? (renderedRect.left + renderedRect.width) : renderedRect.left)
        : (center.x + (halfWidth * direction)),
      y: center.y,
      side: direction > 0 ? "right" : "left",
      tangentX: direction,
      tangentY: 0,
    };
  }

  const direction = deltaY >= 0 ? 1 : -1;
  return {
    x: center.x,
    y: renderedRect
      ? (direction > 0 ? (renderedRect.top + renderedRect.height) : renderedRect.top)
      : (center.y + (halfHeight * direction)),
    side: direction > 0 ? "bottom" : "top",
    tangentX: 0,
    tangentY: direction,
  };
}

function hasFiniteAnchorPoint(point) {
  return Boolean(point)
    && Number.isFinite(Number(point.x))
    && Number.isFinite(Number(point.y));
}

export function createPeripheralDiagramWiringModule({
  state,
  elements,
  gpioBoardLayouts,
  gpioBoardExtraLayouts,
  activeGpioBoardProfile,
  realPeripheralBindingDefinitions,
  helperSignalLabels,
  peripheralHelperBindingValue,
  setPeripheralHelperBindingValue,
  savePeripheralDiagramPositions,
  syncGpioMappingControls,
  queueSettingsSave,
}) {
  let lastRenderedNodes = [];
  let lastRenderedLabelEntries = new Map();
  let lastRenderedLabelRects = new Map();
  const curveDragState = {
    key: "",
    pointerId: null,
    group: null,
    overlay: null,
    pointIndex: -1,
    seedRoutePoints: [],
  };
  const labelConnectDragState = {
    pointerId: null,
    overlay: null,
    sourceRef: "",
    sourceAnchor: null,
    sourcePalette: null,
    previewPath: null,
  };
  const boardEndpointDragState = {
    pointerId: null,
    overlay: null,
    connectionGroup: null,
    previewPath: null,
    sourceRoutePoints: [],
  };
  function defaultSignalLabelEntries(node) {
    const groupKey = String(node?.groupKey || "");
    const profileValue = String(node?.profileValue || "none");
    const index = Number(node?.index || 0);
    const entries = [];
    const used = new Set();
    const pushEntry = (rawLabel) => {
      const label = normalizeSignalLabel(rawLabel);
      const id = peripheralDiagramLabelId(label);
      if (!label || used.has(id)) {
        return;
      }
      used.add(id);
      entries.push({ id, label, order: entries.length });
    };

    for (const definition of realPeripheralBindingDefinitions(groupKey, profileValue, state.settings || {}, index)) {
      pushEntry(definition.label);
    }

    const defaultHelperSignals = isLimitSwitchNode(node)
      ? ["COM"]
      : helperSignalLabels(groupKey, profileValue);

    for (const signalLabel of defaultHelperSignals) {
      pushEntry(signalLabel);
    }

    for (const pinLabel of Array.isArray(node?.pins) ? node.pins : []) {
      if (!isLimitSwitchNode(node) && !isPositivePowerSignal(pinLabel) && !isGroundSignal(pinLabel)) {
        continue;
      }
      pushEntry(pinLabel);
    }

    return entries;
  }

  function resolvedBoardSignalPins() {
    const boardProfile = activeGpioBoardProfile();
    if (!boardProfile) {
      return new Map();
    }

    const boardDefaults = editableBoardLabels(boardProfile);
    const pinById = new Map(
      boardDefaults
        .filter((entry) => Number.isFinite(Number(entry.pin)))
        .map((entry) => [entry.id, Number(entry.pin)]),
    );
    const resolvedBoardLabels = resolvePeripheralDiagramNodeLabels(
      state,
      peripheralDiagramBoardNodeId(boardProfile),
      boardDefaults,
    );

    const boardPinsBySignal = new Map();
    for (const entry of resolvedBoardLabels) {
      const pin = pinById.get(entry.id);
      if (!Number.isFinite(pin)) {
        continue;
      }
      const key = signalKey(entry.label);
      if (!key || boardPinsBySignal.has(key)) {
        continue;
      }
      boardPinsBySignal.set(key, pin);
    }

    return boardPinsBySignal;
  }

  function matchingOptionValue(element, pin) {
    if (!element) {
      return null;
    }
    const match = [...element.options].find((option) => Number(option.value) === Number(pin));
    return match ? String(match.value) : null;
  }

  function applyRealBindingPin(element, pin) {
    const optionValue = matchingOptionValue(element, pin);
    if (!optionValue) {
      return false;
    }
    const changed = String(element.value || "") !== optionValue;
    element.value = optionValue;
    return changed;
  }

  function rewireFromLabels(nodes = Object.values(state.peripheralDiagramNodeMap || {})) {
    const boardPinsBySignal = resolvedBoardSignalPins();
    const usedPins = new Set();
    let matchedAssignments = 0;

    for (const node of nodes) {
      const groupKey = String(node?.groupKey || "");
      const profileValue = String(node?.profileValue || "none");
      const index = Number(node?.index || 0);
      if (!groupKey || profileValue === "none") {
        continue;
      }

      const resolvedLabels = resolvePeripheralDiagramNodeLabels(state, node.id, defaultSignalLabelEntries(node));
      const labelsById = new Map(resolvedLabels.map((entry) => [entry.id, entry]));

      for (const definition of realPeripheralBindingDefinitions(groupKey, profileValue, state.settings || {}, index)) {
        const labelId = peripheralDiagramLabelId(normalizeSignalLabel(definition.label));
        const resolvedLabel = labelsById.get(labelId);
        const pin = boardPinsBySignal.get(signalKey(resolvedLabel?.label || definition.label));
        const optionValue = matchingOptionValue(definition.element, pin);
        if (!optionValue || usedPins.has(optionValue)) {
          continue;
        }
        if (applyRealBindingPin(definition.element, pin)) {
          matchedAssignments += 1;
        }
        usedPins.add(optionValue);
      }

      const realSignalKeys = new Set(
        realPeripheralBindingDefinitions(groupKey, profileValue, state.settings || {}, index)
          .map((definition) => signalKey(definition.label)),
      );
      for (const helperSignal of helperSignalLabels(groupKey, profileValue)) {
        if (realSignalKeys.has(signalKey(helperSignal))) {
          continue;
        }
        const labelId = peripheralDiagramLabelId(normalizeSignalLabel(helperSignal));
        const resolvedLabel = labelsById.get(labelId);
        const pin = boardPinsBySignal.get(signalKey(resolvedLabel?.label || helperSignal));
        const optionValue = String(pin || "").trim();
        if (!optionValue || usedPins.has(optionValue)) {
          continue;
        }
        const existingValue = String(peripheralHelperBindingValue(groupKey, index, helperSignal) || "").trim();
        setPeripheralHelperBindingValue(groupKey, index, helperSignal, optionValue);
        if (existingValue !== optionValue) {
          matchedAssignments += 1;
        }
        usedPins.add(optionValue);
      }
    }

    return { matchedAssignments };
  }

  function clearLabelConnectionPreview() {
    labelConnectDragState.previewPath?.remove();
    labelConnectDragState.pointerId = null;
    labelConnectDragState.overlay = null;
    labelConnectDragState.sourceRef = "";
    labelConnectDragState.sourceAnchor = null;
    labelConnectDragState.sourcePalette = null;
    labelConnectDragState.previewPath = null;
    window.removeEventListener("pointermove", handleLabelConnectionPointerMove);
    window.removeEventListener("pointerup", finishLabelConnectionDrag);
    window.removeEventListener("pointercancel", finishLabelConnectionDrag);
  }

  function clearBoardEndpointPreview() {
    boardEndpointDragState.previewPath?.remove();
    boardEndpointDragState.connectionGroup?.classList.remove("is-endpoint-dragging");
    boardEndpointDragState.pointerId = null;
    boardEndpointDragState.overlay = null;
    boardEndpointDragState.connectionGroup = null;
    boardEndpointDragState.previewPath = null;
    boardEndpointDragState.sourceRoutePoints = [];
    window.removeEventListener("pointermove", handleBoardEndpointPointerMove);
    window.removeEventListener("pointerup", finishBoardEndpointDrag);
    window.removeEventListener("pointercancel", finishBoardEndpointDrag);
  }

  function previewConnectionPath(sourceAnchor, pointerPoint) {
    if (!hasFiniteAnchorPoint(sourceAnchor) || !hasFiniteAnchorPoint(pointerPoint)) {
      return "";
    }
    return smoothBezierPath(dedupeAdjacentPoints([
      { x: sourceAnchor.x, y: sourceAnchor.y },
      anchorLeadPoint(sourceAnchor, sourceAnchor),
      { x: pointerPoint.x, y: pointerPoint.y },
    ]));
  }

  function handleLabelConnectionPointerMove(event) {
    if (labelConnectDragState.pointerId !== event.pointerId || !labelConnectDragState.previewPath || !labelConnectDragState.sourceAnchor) {
      return;
    }
    const stageRect = diagramRect(elements.peripheralDiagramStage);
    if (!stageRect?.width || !stageRect?.height) {
      return;
    }
    const pointerPoint = {
      x: clampValue(canvasClientPoint(event,elements.peripheralDiagramStage).clientX - stageRect.left, 0, stageRect.width),
      y: clampValue(canvasClientPoint(event,elements.peripheralDiagramStage).clientY - stageRect.top, 0, stageRect.height),
    };
    labelConnectDragState.previewPath.setAttribute("d", previewConnectionPath(labelConnectDragState.sourceAnchor, pointerPoint));
    event.preventDefault();
  }

  function finishLabelConnectionDrag(event) {
    if (labelConnectDragState.pointerId !== event.pointerId || !labelConnectDragState.sourceRef) {
      return;
    }
    const targetElement = document.elementFromPoint(event.clientX, event.clientY)?.closest?.(".peripheral-diagram-floating-label[data-node-id][data-label-key]");
    const sourceRef = labelConnectDragState.sourceRef;
    const targetRef = targetElement
      ? labelReferenceKey(targetElement.dataset.nodeId, targetElement.dataset.labelKey)
      : "";
    const shouldSave = targetRef && targetRef !== sourceRef;
    if (shouldSave) {
      const [fromNodeId, fromLabelKey] = sourceRef.split(":");
      const [toNodeId, toLabelKey] = targetRef.split(":");
      const changed = upsertCustomLabelConnection(state, {
        fromNodeId,
        fromLabelKey,
        toNodeId,
        toLabelKey,
      });
      if (changed) {
        savePeripheralDiagramPositions?.();
        render(lastRenderedNodes);
      }
    }
    clearLabelConnectionPreview();
    event.preventDefault();
  }

  function beginLabelConnectionDrag(event, overlay, sourceEntry, sourceRect) {
    if (event.button !== 0 || !sourceEntry || !overlay) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    const sourceRef = labelReferenceKey(sourceEntry.nodeId, sourceEntry.labelKey);
    const sourceAnchor = labelAnchorAwayFromOwner(
      sourceEntry,
      sourceEntry.nodeRect,
      overlay.viewBox.baseVal.width,
      overlay.viewBox.baseVal.height,
      sourceRect || null,
    );
    const previewPath = createSvgElement("path");
    const sourcePalette = normalizeWirePalette(sourceEntry.palette);
    previewPath.setAttribute("class", "peripheral-diagram-wire peripheral-diagram-wire-preview");
    previewPath.setAttribute("stroke", sourcePalette.stroke);
    overlay.appendChild(previewPath);

    labelConnectDragState.pointerId = event.pointerId;
    labelConnectDragState.overlay = overlay;
    labelConnectDragState.sourceRef = sourceRef;
    labelConnectDragState.sourceAnchor = sourceAnchor;
    labelConnectDragState.sourcePalette = sourcePalette;
    labelConnectDragState.previewPath = previewPath;

    handleLabelConnectionPointerMove(event);
    window.addEventListener("pointermove", handleLabelConnectionPointerMove);
    window.addEventListener("pointerup", finishLabelConnectionDrag);
    window.addEventListener("pointercancel", finishLabelConnectionDrag);
  }

  function bindLabelConnectionInteractions(labelLayer, overlay) {
    labelLayer.querySelectorAll(".peripheral-diagram-floating-label-pill").forEach((pill) => {
      pill.addEventListener("pointerdown", (event) => {
        const labelElement = pill.closest(".peripheral-diagram-floating-label[data-node-id][data-label-key]");
        if (!labelElement) {
          return;
        }
        const ref = labelReferenceKey(labelElement.dataset.nodeId, labelElement.dataset.labelKey);
        beginLabelConnectionDrag(event, overlay, lastRenderedLabelEntries.get(ref) || null, lastRenderedLabelRects.get(ref) || null);
      });
    });
  }

  function boardEndpointGeometry(group, boardAnchor, routePoints) {
    const nodeAnchor = {
      x: Number(group?.dataset.nodeAnchorX || 0),
      y: Number(group?.dataset.nodeAnchorY || 0),
      side: String(group?.dataset.nodeAnchorSide || "right"),
    };
    const nodeRect = {
      left: Number(group?.dataset.nodeRectLeft || 0),
      top: Number(group?.dataset.nodeRectTop || 0),
      width: Number(group?.dataset.nodeRectWidth || 0),
      height: Number(group?.dataset.nodeRectHeight || 0),
    };
    const boardRect = {
      left: Number(group?.dataset.boardRectLeft || 0),
      top: Number(group?.dataset.boardRectTop || 0),
      width: Number(group?.dataset.boardRectWidth || 0),
      height: Number(group?.dataset.boardRectHeight || 0),
    };
    return connectionGeometry(nodeAnchor, boardAnchor, nodeRect, boardRect, routePoints);
  }

  function boardLabelEntryFromPoint(clientX, clientY) {
    const boardNodeId = peripheralDiagramBoardNodeId(activeGpioBoardProfile());
    const targetElement = document.elementFromPoint(clientX, clientY)?.closest?.(".peripheral-diagram-floating-label[data-node-id][data-label-key]");
    if (targetElement) {
      const targetRef = labelReferenceKey(targetElement.dataset.nodeId, targetElement.dataset.labelKey);
      const entry = lastRenderedLabelEntries.get(targetRef);
      if (entry?.nodeId === boardNodeId && Number.isFinite(Number(entry.pin))) {
        return entry;
      }
    }

    let nearestEntry = null;
    let nearestDistance = Infinity;
    for (const entry of lastRenderedLabelEntries.values()) {
      if (entry?.nodeId !== boardNodeId || !Number.isFinite(Number(entry.pin))) {
        continue;
      }
      const rect = lastRenderedLabelRects.get(labelReferenceKey(entry.nodeId, entry.labelKey));
      if (!rect) {
        continue;
      }
      const centerX = rect.left + (rect.width / 2);
      const centerY = rect.top + (rect.height / 2);
      const stageRect = diagramRect(elements.peripheralDiagramStage);
      if (!stageRect) {
        continue;
      }
      const deltaX = canvasClientPoint({clientX,clientY},elements.peripheralDiagramStage).clientX - stageRect.left - centerX;
      const deltaY = canvasClientPoint({clientX,clientY},elements.peripheralDiagramStage).clientY - stageRect.top - centerY;
      const distance = Math.hypot(deltaX, deltaY);
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearestEntry = entry;
      }
    }
    return nearestDistance <= 42 ? nearestEntry : null;
  }

  function clearRealBindingPin(element) {
    if (!element) {
      return false;
    }
    const fallbackOption = [...element.options].find((option) => {
      const value = String(option.value ?? "").trim();
      return value === "" || value === "0" || Number(value) < 0;
    });
    if (!fallbackOption) {
      return false;
    }
    const nextValue = String(fallbackOption.value ?? "");
    const changed = String(element.value ?? "") !== nextValue;
    element.value = nextValue;
    return changed;
  }

  function applyConnectionPinAssignment(connectionGroup, targetBoardEntry) {
    const nodeId = String(connectionGroup?.dataset.nodeId || "");
    const signal = String(connectionGroup?.dataset.signal || "").trim().toUpperCase();
    const node = state.peripheralDiagramNodeMap?.[nodeId];
    if (!nodeId || !signal || !node) {
      return false;
    }

    const groupKey = String(node.groupKey || "");
    const profileValue = String(node.profileValue || "none");
    const index = Number(node.index || 0);

    if (isLimitSwitchNode(node)) {
      if (signal === "COM") {
        const targetPin = Number(targetBoardEntry?.pin ?? NaN);
        if (!Number.isFinite(targetPin) || targetPin < 0) {
          return false;
        }
        const nextValue = String(targetPin);
        const existingValue = String(peripheralHelperBindingValue(groupKey, index, "COM") || peripheralHelperBindingValue(groupKey, index, "SIG") || "").trim();
        if (existingValue === nextValue) {
          return false;
        }
        setPeripheralHelperBindingValue(groupKey, index, "COM", nextValue);
        if (existingValue && existingValue !== nextValue) {
          setPeripheralHelperBindingValue(groupKey, index, "SIG", "");
        }
        syncGpioMappingControls?.();
        queueSettingsSave?.(0);
        render(lastRenderedNodes);
        return true;
      }

      if (signal === "NC" || signal === "NO") {
        const sourceValue = limitSwitchSourceValueForBoardEntry(targetBoardEntry);
        if (!sourceValue) {
          return false;
        }
        const currentContact = String(peripheralHelperBindingValue(groupKey, index, "CONTACT") || "NO").trim().toUpperCase() === "NC" ? "NC" : "NO";
        const currentSource = String(peripheralHelperBindingValue(groupKey, index, "SOURCE") || "GND").trim().toUpperCase() === "VCC" ? "VCC" : "GND";
        if (currentContact === signal && currentSource === sourceValue) {
          return false;
        }
        setPeripheralHelperBindingValue(groupKey, index, "CONTACT", signal);
        setPeripheralHelperBindingValue(groupKey, index, "SOURCE", sourceValue);
        syncGpioMappingControls?.();
        queueSettingsSave?.(0);
        render(lastRenderedNodes);
        return true;
      }
    }

    const targetPin = Number(targetBoardEntry?.pin ?? NaN);
    if (!Number.isFinite(targetPin) || targetPin < 0) {
      return false;
    }

    let changed = false;

    for (const definition of realPeripheralBindingDefinitions(groupKey, profileValue, state.settings || {}, index)) {
      if (signalKey(definition.label) !== signal) {
        continue;
      }
      const optionValue = matchingOptionValue(definition.element, targetPin);
      if (!optionValue) {
        return false;
      }
      changed = applyRealBindingPin(definition.element, targetPin);
      if (changed) {
        definition.element.dispatchEvent(new Event("change", { bubbles: true }));
      }
      break;
    }

    if (!changed) {
      for (const helperSignal of helperSignalLabels(groupKey, profileValue)) {
        if (signalKey(helperSignal) !== signal) {
          continue;
        }
        const nextValue = String(targetPin);
        const existingValue = String(peripheralHelperBindingValue(groupKey, index, helperSignal) || "").trim();
        if (existingValue !== nextValue) {
          setPeripheralHelperBindingValue(groupKey, index, helperSignal, nextValue);
          changed = true;
        }
        break;
      }
    }

    if (!changed) {
      return false;
    }

    syncGpioMappingControls?.();
    queueSettingsSave?.(0);
    render(lastRenderedNodes);
    return true;
  }

  function clearConnectionPinAssignment(connectionGroup) {
    const nodeId = String(connectionGroup?.dataset.nodeId || "");
    const signal = String(connectionGroup?.dataset.signal || "").trim().toUpperCase();
    const node = state.peripheralDiagramNodeMap?.[nodeId];
    if (!nodeId || !signal || !node) {
      return false;
    }

    const groupKey = String(node.groupKey || "");
    const profileValue = String(node.profileValue || "none");
    const index = Number(node.index || 0);

    if (isLimitSwitchNode(node) && (signal === "NC" || signal === "NO")) {
      const currentContact = String(peripheralHelperBindingValue(groupKey, index, "CONTACT") || "NO").trim().toUpperCase() === "NC" ? "NC" : "NO";
      const currentSource = String(peripheralHelperBindingValue(groupKey, index, "SOURCE") || "").trim();
      if (currentContact !== signal || !currentSource) {
        return false;
      }
      setPeripheralHelperBindingValue(groupKey, index, "SOURCE", "");
      syncGpioMappingControls?.();
      queueSettingsSave?.(0);
      render(lastRenderedNodes);
      return true;
    }

    let changed = false;

    for (const definition of realPeripheralBindingDefinitions(groupKey, profileValue, state.settings || {}, index)) {
      if (signalKey(definition.label) !== signal) {
        continue;
      }
      changed = clearRealBindingPin(definition.element);
      if (changed) {
        definition.element.dispatchEvent(new Event("change", { bubbles: true }));
      }
      break;
    }

    if (!changed) {
      for (const helperSignal of helperSignalLabels(groupKey, profileValue)) {
        if (signalKey(helperSignal) !== signal) {
          continue;
        }
        const existingValue = String(peripheralHelperBindingValue(groupKey, index, helperSignal) || "").trim();
        if (existingValue) {
          setPeripheralHelperBindingValue(groupKey, index, helperSignal, "");
          changed = true;
        }
        break;
      }
    }

    if (!changed) {
      return false;
    }

    syncGpioMappingControls?.();
    queueSettingsSave?.(0);
    render(lastRenderedNodes);
    return true;
  }

  function removeCustomLabelConnection(connectionKey) {
    const normalizedKey = String(connectionKey || "").replace(/^custom:/, "");
    const current = readCustomLabelConnections(state);
    const next = current.filter((entry) => customLabelConnectionKey(entry) !== normalizedKey);
    if (next.length === current.length) {
      return false;
    }
    writeCustomLabelConnections(state, next);
    clearStoredWireCurveByKey(state, connectionKey);
    savePeripheralDiagramPositions?.();
    render(lastRenderedNodes);
    return true;
  }

  function deleteWireConnection(connectionGroup) {
    if (!connectionGroup) {
      return false;
    }
    const connectionKind = String(connectionGroup.dataset.connectionKind || "auto");
    const connectionType = String(connectionGroup.dataset.connectionType || "");
    const connectionKey = String(connectionGroup.dataset.connectionKey || "");

    if (connectionKind === "custom") {
      return removeCustomLabelConnection(connectionKey);
    }

    if (connectionType === "gpio") {
      const changed = clearConnectionPinAssignment(connectionGroup);
      if (changed) {
        clearStoredWireCurveByKey(state, connectionKey);
        savePeripheralDiagramPositions?.();
      }
      return changed;
    }

    const hidden = hideConnection(state, connectionKey);
    if (!hidden) {
      return false;
    }
    clearStoredWireCurveByKey(state, connectionKey);
    savePeripheralDiagramPositions?.();
    render(lastRenderedNodes);
    return true;
  }

  function handleBoardEndpointPointerMove(event) {
    if (boardEndpointDragState.pointerId !== event.pointerId || !boardEndpointDragState.previewPath || !boardEndpointDragState.connectionGroup) {
      return;
    }
    const stageRect = diagramRect(elements.peripheralDiagramStage);
    if (!stageRect?.width || !stageRect?.height) {
      return;
    }
    const boardAnchor = {
      x: clampValue(canvasClientPoint(event,elements.peripheralDiagramStage).clientX - stageRect.left, 0, stageRect.width),
      y: clampValue(canvasClientPoint(event,elements.peripheralDiagramStage).clientY - stageRect.top, 0, stageRect.height),
      side: String(boardEndpointDragState.connectionGroup.dataset.boardAnchorSide || "left"),
    };
    const geometry = boardEndpointGeometry(boardEndpointDragState.connectionGroup, boardAnchor, boardEndpointDragState.sourceRoutePoints);
    boardEndpointDragState.previewPath.setAttribute("d", geometry.path);
    event.preventDefault();
  }

  function finishBoardEndpointDrag(event) {
    if (boardEndpointDragState.pointerId !== event.pointerId || !boardEndpointDragState.connectionGroup) {
      return;
    }
    const targetBoardEntry = boardLabelEntryFromPoint(event.clientX, event.clientY);
    if (targetBoardEntry) {
      applyConnectionPinAssignment(boardEndpointDragState.connectionGroup, targetBoardEntry);
    } else {
      clearConnectionPinAssignment(boardEndpointDragState.connectionGroup);
    }
    clearBoardEndpointPreview();
    event.preventDefault();
  }

  function beginBoardEndpointDrag(event, overlay, connectionGroup) {
    if (event.button !== 0 || !overlay || !connectionGroup) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    setActiveConnection(overlay, connectionGroup);
    connectionGroup.classList.add("is-active", "is-endpoint-dragging");

    const previewPath = createSvgElement("path");
    const stroke = String(connectionGroup.querySelector(".peripheral-diagram-wire")?.getAttribute("stroke") || "#0f766e");
    previewPath.setAttribute("class", "peripheral-diagram-wire peripheral-diagram-wire-preview");
    previewPath.setAttribute("stroke", stroke);
    overlay.appendChild(previewPath);

    boardEndpointDragState.pointerId = event.pointerId;
    boardEndpointDragState.overlay = overlay;
    boardEndpointDragState.connectionGroup = connectionGroup;
    boardEndpointDragState.previewPath = previewPath;
    boardEndpointDragState.sourceRoutePoints = cloneCurvePoints(JSON.parse(connectionGroup.dataset.routePoints || "[]"));

    handleBoardEndpointPointerMove(event);
    window.addEventListener("pointermove", handleBoardEndpointPointerMove);
    window.addEventListener("pointerup", finishBoardEndpointDrag);
    window.addEventListener("pointercancel", finishBoardEndpointDrag);
  }

  function renderCurveHandles(group, overlay, connectionKey, geometry) {
    if (!group || !geometry) {
      return;
    }
    group.querySelectorAll(".peripheral-diagram-wire-handle").forEach((handle) => handle.remove());
    const handlePoints = cloneCurvePoints(geometry.handlePoints);
    handlePoints.forEach((point, index) => {
      const handle = createSvgElement("circle");
      handle.setAttribute("class", "peripheral-diagram-wire-handle");
      handle.setAttribute("cx", String(point.x));
      handle.setAttribute("cy", String(point.y));
      handle.setAttribute("r", "6");
      handle.addEventListener("pointerdown", (event) => beginCurvePointerDrag(event, overlay, group, connectionKey, index, false, handlePoints));
      handle.addEventListener("contextmenu", (event) => {
        event.preventDefault();
        event.stopPropagation();
        removeCurvePoint(group, overlay, connectionKey, index, handlePoints);
      });
      group.appendChild(handle);
    });
  }

  function removeCurvePoint(group, overlay, connectionKey, pointIndex, seedRoutePoints = null) {
    if (!group || pointIndex < 0) {
      return;
    }
    const routePoints = cloneCurvePoints(seedRoutePoints?.length ? seedRoutePoints : JSON.parse(group.dataset.routePoints || "[]"));
    if (pointIndex >= routePoints.length) {
      return;
    }
    routePoints.splice(pointIndex, 1);
    updateCurveGroupPath(group, overlay, connectionKey, routePoints);
    const stageRect = diagramRect(elements.peripheralDiagramStage);
    if (stageRect?.width && stageRect?.height) {
      writeStoredWireCurve(state, connectionKey, routePoints, stageRect);
      savePeripheralDiagramPositions?.();
    }
  }

  function updateCurveGroupPath(group, overlay, connectionKey, routePoints) {
    if (!group) {
      return;
    }
    const nodeAnchor = {
      x: Number(group.dataset.nodeAnchorX || 0),
      y: Number(group.dataset.nodeAnchorY || 0),
      side: String(group.dataset.nodeAnchorSide || "right"),
    };
    const boardAnchor = {
      x: Number(group.dataset.boardAnchorX || 0),
      y: Number(group.dataset.boardAnchorY || 0),
      side: String(group.dataset.boardAnchorSide || "left"),
    };
    const nodeRect = {
      left: Number(group.dataset.nodeRectLeft || 0),
      top: Number(group.dataset.nodeRectTop || 0),
      width: Number(group.dataset.nodeRectWidth || 0),
      height: Number(group.dataset.nodeRectHeight || 0),
    };
    const boardRect = {
      left: Number(group.dataset.boardRectLeft || 0),
      top: Number(group.dataset.boardRectTop || 0),
      width: Number(group.dataset.boardRectWidth || 0),
      height: Number(group.dataset.boardRectHeight || 0),
    };
    const geometry = connectionGeometry(nodeAnchor, boardAnchor, nodeRect, boardRect, routePoints);
    group.querySelector(".peripheral-diagram-wire-glow")?.setAttribute("d", geometry.path);
    group.querySelector(".peripheral-diagram-wire")?.setAttribute("d", geometry.path);
    group.querySelector(".peripheral-diagram-wire-hit")?.setAttribute("d", geometry.path);
    group.dataset.routePoints = JSON.stringify(geometry.manualCurvePoints);
    group.dataset.controlX = String(geometry.controlPoint.x);
    group.dataset.controlY = String(geometry.controlPoint.y);
    renderCurveHandles(group, overlay, connectionKey, geometry);
  }

  function beginCurvePointerDrag(event, overlay, connectionGroup, connectionKey, pointIndex = -1, insertUnderPointer = false, seedRoutePoints = null) {
    if (event.button !== 0) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    connectionGroup.classList.add("is-active", "is-dragging");
    setActiveConnection(overlay, connectionGroup);
    curveDragState.key = connectionKey;
    curveDragState.pointerId = event.pointerId;
    curveDragState.group = connectionGroup;
    curveDragState.overlay = overlay;
    curveDragState.pointIndex = pointIndex;
    curveDragState.seedRoutePoints = cloneCurvePoints(seedRoutePoints?.length ? seedRoutePoints : JSON.parse(connectionGroup.dataset.routePoints || "[]"));

    if (!JSON.parse(connectionGroup.dataset.routePoints || "[]").length && curveDragState.seedRoutePoints.length) {
      connectionGroup.dataset.routePoints = JSON.stringify(curveDragState.seedRoutePoints);
    }

    if (insertUnderPointer) {
      handleCurvePointerMove(event);
    }

    window.addEventListener("pointermove", handleCurvePointerMove);
    window.addEventListener("pointerup", finishCurvePointerDrag);
    window.addEventListener("pointercancel", finishCurvePointerDrag);
  }

  function handleCurvePointerMove(event) {
    if (curveDragState.pointerId !== event.pointerId || !curveDragState.key || !curveDragState.group) {
      return;
    }
    const stageRect = diagramRect(elements.peripheralDiagramStage);
    if (!stageRect?.width || !stageRect?.height) {
      return;
    }
    const group = curveDragState.group;
    const nodeAnchor = {
      x: Number(group.dataset.nodeAnchorX || 0),
      y: Number(group.dataset.nodeAnchorY || 0),
      side: String(group.dataset.nodeAnchorSide || "right"),
    };
    const boardAnchor = {
      x: Number(group.dataset.boardAnchorX || 0),
      y: Number(group.dataset.boardAnchorY || 0),
      side: String(group.dataset.boardAnchorSide || "left"),
    };
    const routePoints = cloneCurvePoints(JSON.parse(group.dataset.routePoints || "[]"));
    const pointerPoint = {
      x: clampValue(canvasClientPoint(event,elements.peripheralDiagramStage).clientX - stageRect.left, 0, stageRect.width),
      y: clampValue(canvasClientPoint(event,elements.peripheralDiagramStage).clientY - stageRect.top, 0, stageRect.height),
    };
    let nextRoutePoints = routePoints;
    if (curveDragState.pointIndex < 0) {
      const insertion = insertPointIntoRoute(routePoints, pointerPoint, nodeAnchor, boardAnchor);
      nextRoutePoints = insertion.points;
      curveDragState.pointIndex = insertion.index;
    } else {
      nextRoutePoints = [...routePoints];
      nextRoutePoints[curveDragState.pointIndex] = pointerPoint;
    }
    updateCurveGroupPath(group, curveDragState.overlay, curveDragState.key, nextRoutePoints);
    event.preventDefault();
  }

  function finishCurvePointerDrag(event) {
    if (curveDragState.pointerId !== event.pointerId || !curveDragState.key || !curveDragState.group) {
      return;
    }
    const stageRect = diagramRect(elements.peripheralDiagramStage);
    if (stageRect?.width && stageRect?.height) {
      writeStoredWireCurve(
        state,
        curveDragState.key,
        cloneCurvePoints(JSON.parse(curveDragState.group.dataset.routePoints || "[]")),
        stageRect,
      );
      savePeripheralDiagramPositions?.();
    }
    curveDragState.group.classList.remove("is-dragging");
    curveDragState.key = "";
    curveDragState.pointerId = null;
    curveDragState.group = null;
    curveDragState.overlay = null;
    curveDragState.pointIndex = -1;
    curveDragState.seedRoutePoints = [];
    window.removeEventListener("pointermove", handleCurvePointerMove);
    window.removeEventListener("pointerup", finishCurvePointerDrag);
    window.removeEventListener("pointercancel", finishCurvePointerDrag);
    event.preventDefault();
  }

  function resetManualWireCurves(nodes = lastRenderedNodes.length ? lastRenderedNodes : Object.values(state.peripheralDiagramNodeMap || {})) {
    const clearedCurves = clearWireCurveStore(state);
    const clearedCustomConnections = clearCustomLabelWireStore(state);
    const clearedHiddenConnections = clearHiddenConnectionStore(state);
    const cleared = clearedCurves || clearedCustomConnections || clearedHiddenConnections;
    if (cleared) {
      savePeripheralDiagramPositions?.();
    }
    render(nodes);
    return { cleared };
  }

  function ensureOverlay() {
    const stage = elements.peripheralDiagramStage;
    if (!stage) {
      return null;
    }

    let overlay = stage.querySelector(".peripheral-diagram-wiring-overlay");
    if (!overlay) {
      overlay = createSvgElement("svg");
      overlay.classList.add("peripheral-diagram-wiring-overlay");
      overlay.setAttribute("aria-hidden", "true");
      overlay.addEventListener("contextmenu", (event) => event.preventDefault());
      const boardImage = elements.peripheralDiagramBoardImage;
      if (boardImage?.parentElement === stage) {
        stage.insertBefore(overlay, boardImage.nextSibling);
      } else {
        stage.appendChild(overlay);
      }
    }
    if (!stage.dataset.contextMenuDisabled) {
      stage.addEventListener("contextmenu", (event) => event.preventDefault());
      stage.dataset.contextMenuDisabled = "true";
    }
    return overlay;
  }

  function boardAnchors(stageRect, boardRect) {
    const boardProfile = activeGpioBoardProfile();
    const primary = gpioBoardLayouts[boardProfile] || { left: [], right: [] };
    const extra = gpioBoardExtraLayouts[boardProfile] || { left: [], right: [] };
    const calibration = boardAnchorCalibration(boardProfile);
    const anchorsByKey = new Map();

    const buildSideAnchors = (entries, side, lane) => {
      const validEntries = entries.filter((entry) => entry && (entry.pin !== undefined || entry.label));
      if (!validEntries.length) {
        return;
      }
      const topInset = boardRect.height * calibration.topInsetRatio;
      const bottomInset = boardRect.height * calibration.bottomInsetRatio;
      const usableHeight = Math.max(boardRect.height - topInset - bottomInset, 0);
      const step = validEntries.length > 1 ? (usableHeight / (validEntries.length - 1)) : 0;
      validEntries.forEach((entry, index) => {
        const y = boardRect.top + topInset + (step * index);
        const outerOffset = calibration.outerOffset + (lane * calibration.extraLaneGap);
        const x = side === "left"
          ? boardRect.left - outerOffset
          : boardRect.left + boardRect.width + outerOffset;
        const boardLabel = String(entry.label || (entry.pin != null ? `GPIO${entry.pin}` : "")).trim();
        if (!boardLabel) {
          return;
        }
        const key = entry.pin != null ? `gpio:${entry.pin}` : `rail:${boardLabel.toUpperCase()}`;
        if (!anchorsByKey.has(key)) {
          anchorsByKey.set(key, []);
        }
        anchorsByKey.get(key).push({ x, y, side, lane, boardLabel, pin: entry.pin, key });
      });
    };

    buildSideAnchors(primary.left || [], "left", 0);
    buildSideAnchors(primary.right || [], "right", 0);
    buildSideAnchors(extra.left || [], "left", 1);
    buildSideAnchors(extra.right || [], "right", 1);
    return anchorsByKey;
  }

  function editableBoardLabels(boardProfile) {
    const primary = gpioBoardLayouts[boardProfile] || { left: [], right: [] };
    const extra = gpioBoardExtraLayouts[boardProfile] || { left: [], right: [] };
    const labels = [];
    const seenRailLabels = new Set();

    const pushLabels = (entries, side, lane) => {
      const validEntries = entries.filter((entry) => entry && (entry.pin !== undefined || entry.label));
      validEntries.forEach((entry, index) => {
        const label = String(entry.label || (entry.pin != null ? `GPIO${entry.pin}` : "")).trim();
        if (!label) {
          return;
        }
        const railKey = signalKey(label);
        if ((isPositivePowerSignal(label) || isGroundSignal(label)) && seenRailLabels.has(railKey)) {
          return;
        }
        if (isPositivePowerSignal(label) || isGroundSignal(label)) {
          seenRailLabels.add(railKey);
        }
        labels.push({
          id: peripheralDiagramBoardLabelEntryId({ pin: entry.pin, label, side, lane, index }),
          label,
          pin: entry.pin,
          ...peripheralDiagramBoardLabelDefaultLayout({ side, lane, index, count: validEntries.length }),
          coordinateSpace: "visual",
        });
      });
    };

    pushLabels(primary.left || [], "left", 0);
    pushLabels(primary.right || [], "right", 0);
    pushLabels(extra.left || [], "left", 1);
    pushLabels(extra.right || [], "right", 1);
    return labels;
  }

  function collectConnections(nodes) {
    return nodes.flatMap((node) => {
      const groupKey = String(node?.groupKey || "");
      const profileValue = String(node?.profileValue || "none");
      const index = Number(node?.index || 0);
      if (!groupKey || profileValue === "none") {
        return [];
      }

      const connections = [];
      const usedSignals = new Set();
      const realBindings = realPeripheralBindingDefinitions(groupKey, profileValue, state.settings || {}, index);
      for (const binding of realBindings) {
        const pin = Number(binding?.element?.value ?? NaN);
        if (!Number.isFinite(pin) || pin < 0) {
          continue;
        }
        const signalLabel = normalizeSignalLabel(binding.label);
        connections.push({
          nodeId: node.id,
          signalLabel,
          pin,
          type: "gpio",
          boardLabel: selectedOptionText(binding.element, `GPIO${pin}`),
        });
        usedSignals.add(signalKey(signalLabel));
      }

      for (const signalLabel of helperSignalLabels(groupKey, profileValue)) {
        const normalizedSignal = normalizeSignalLabel(signalLabel);
        if (usedSignals.has(signalKey(normalizedSignal))) {
          continue;
        }
        const pinValue = peripheralHelperBindingValue(groupKey, index, signalLabel);
        const pin = Number(pinValue || NaN);
        if (!Number.isFinite(pin) || pin < 0) {
          continue;
        }
        connections.push({
          nodeId: node.id,
          signalLabel: normalizedSignal,
          pin,
          type: "gpio",
          boardLabel: `GPIO${pin}`,
        });
        usedSignals.add(signalKey(normalizedSignal));
      }

      if (isLimitSwitchNode(node)) {
        const contactSignal = String(peripheralHelperBindingValue(groupKey, index, "CONTACT") || "NO").trim().toUpperCase() === "NC" ? "NC" : "NO";
        const sourceValue = String(peripheralHelperBindingValue(groupKey, index, "SOURCE") || "GND").trim().toUpperCase() === "VCC" ? "VCC" : "GND";
        if (!usedSignals.has(contactSignal)) {
          connections.push({
            nodeId: node.id,
            signalLabel: contactSignal,
            type: "rail",
            boardLabel: limitSwitchBoardLabelForSource(node, sourceValue),
          });
          usedSignals.add(contactSignal);
        }
      }

      for (const pinLabel of Array.isArray(node.pins) ? node.pins : []) {
        if (groupKey === "power") {
          continue;
        }
        if (!isPositivePowerSignal(pinLabel) && !isGroundSignal(pinLabel)) {
          continue;
        }
        const normalizedSignal = normalizeSignalLabel(pinLabel);
        if (usedSignals.has(signalKey(normalizedSignal))) {
          continue;
        }
        const boardLabel = powerRailLabelForNode(node, pinLabel);
        if (!boardLabel) {
          continue;
        }
        connections.push({
          nodeId: node.id,
          signalLabel: normalizedSignal,
          type: "rail",
          boardLabel,
        });
        usedSignals.add(signalKey(normalizedSignal));
      }

      return connections;
    });
  }

  function chooseBoardAnchor(anchorCandidates, connection, nodeAnchor) {
    const candidates = anchorCandidates.get(boardTargetKey(connection)) || [];
    if (!candidates.length) {
      return null;
    }
    return [...candidates].sort((left, right) => {
      const leftScore = Math.abs(left.y - nodeAnchor.y) + (left.side === nodeAnchor.side ? 40 : 0) + (left.lane * 6);
      const rightScore = Math.abs(right.y - nodeAnchor.y) + (right.side === nodeAnchor.side ? 40 : 0) + (right.lane * 6);
      return leftScore - rightScore;
    })[0];
  }

  function drawSignalBadge(container, anchor, signalLabel, palette) {
    const group = createSvgElement("g");
    group.setAttribute("class", "peripheral-diagram-wire-badge");

    const label = normalizeSignalLabel(signalLabel) || "SIG";
    const width = Math.max(28, (label.length * 6.6) + 14);
    const height = 18;
    const rect = createSvgElement("rect");
    const text = createSvgElement("text");
    const sideOffset = anchor.side === "left"
      ? 8
      : (anchor.side === "right"
        ? -(width + 8)
        : -(width / 2));
    const verticalOffset = anchor.side === "top" ? 8 : -26;
    const x = anchor.x + sideOffset;
    const y = anchor.y + verticalOffset;

    rect.setAttribute("x", String(x));
    rect.setAttribute("y", String(y));
    rect.setAttribute("rx", "9");
    rect.setAttribute("ry", "9");
    rect.setAttribute("width", String(width));
    rect.setAttribute("height", String(height));
    rect.setAttribute("fill", palette.badge);
    rect.setAttribute("fill-opacity", "0.94");

    text.setAttribute("x", String(x + (width / 2)));
    text.setAttribute("y", String(y + 12.2));
    text.setAttribute("text-anchor", "middle");
    text.setAttribute("fill", palette.text);
    text.textContent = label;

    group.append(rect, text);
    container.appendChild(group);
  }

  function drawBoardMarker(container, anchor, palette) {
    const ring = createSvgElement("circle");
    ring.setAttribute("cx", String(anchor.x));
    ring.setAttribute("cy", String(anchor.y));
    ring.setAttribute("r", "5.5");
    ring.setAttribute("class", "peripheral-diagram-wire-board-marker");
    ring.setAttribute("fill", "#ffffff");
    ring.setAttribute("stroke", palette.stroke);
    ring.setAttribute("stroke-width", "2.4");
    container.appendChild(ring);
    return ring;
  }

  function drawBoardBadge(container, anchor, boardLabel, palette) {
    const group = createSvgElement("g");
    group.setAttribute("class", "peripheral-diagram-wire-board-badge");

    const label = String(boardLabel || "").trim() || "GPIO";
    const width = Math.max(34, (label.length * 6.6) + 16);
    const height = 18;
    const rect = createSvgElement("rect");
    const text = createSvgElement("text");
    const sideOffset = anchor.side === "left" ? -(width + 10) : 10;
    const x = anchor.x + sideOffset;
    const y = anchor.y - 9;

    rect.setAttribute("x", String(x));
    rect.setAttribute("y", String(y));
    rect.setAttribute("rx", "9");
    rect.setAttribute("ry", "9");
    rect.setAttribute("width", String(width));
    rect.setAttribute("height", String(height));
    rect.setAttribute("fill", "#ffffff");
    rect.setAttribute("stroke", palette.stroke);
    rect.setAttribute("stroke-width", "1.4");

    text.setAttribute("x", String(x + (width / 2)));
    text.setAttribute("y", String(y + 12.2));
    text.setAttribute("text-anchor", "middle");
    text.setAttribute("fill", palette.stroke);
    text.textContent = label;

    group.append(rect, text);
    container.appendChild(group);
  }

  function drawNodeMarker(container, anchor, palette) {
    const ring = createSvgElement("circle");
    ring.setAttribute("cx", String(anchor.x));
    ring.setAttribute("cy", String(anchor.y));
    ring.setAttribute("r", "4.2");
    ring.setAttribute("fill", palette.stroke);
    ring.setAttribute("fill-opacity", "0.98");
    container.appendChild(ring);
    return ring;
  }

  function render(nodes = Object.values(state.peripheralDiagramNodeMap || {})) {
    lastRenderedNodes = [...nodes];
    const overlay = ensureOverlay();
    const stage = elements.peripheralDiagramStage;
    const boardImage = elements.peripheralDiagramBoardImage;
    const labelLayer = ensureLabelLayer(stage);
    if (!overlay || !stage || !boardImage || !labelLayer) {
      return;
    }

    const stageRect = diagramRect(stage);
    const boardClientRect = diagramRect(boardImage);
    if (!stageRect.width || !stageRect.height || !boardClientRect.width || !boardClientRect.height) {
      overlay.innerHTML = "";
      labelLayer.innerHTML = "";
      return;
    }

    const boardRect = {
      left: boardClientRect.left - stageRect.left,
      top: boardClientRect.top - stageRect.top,
      width: boardClientRect.width,
      height: boardClientRect.height,
    };
    const anchorCandidates = boardAnchors(stageRect, boardRect);
    const nodeRects = new Map(
      nodes.map((node) => {
        const element = elements.peripheralDiagramItems?.querySelector(`[data-node-id="${node.id}"]`);
        const rect = relativeRectForElement(element, stageRect);
        return [
          node.id,
          rect,
        ];
      }),
    );

    const visualRects = new Map(
      nodes.map((node) => {
        const element = elements.peripheralDiagramItems?.querySelector(`[data-node-id="${node.id}"]`);
        return [
          node.id,
          nodeVisualRect(element, stageRect),
        ];
      }),
    );

    overlay.innerHTML = "";
    overlay.setAttribute("viewBox", `0 0 ${stageRect.width} ${stageRect.height}`);
    overlay.setAttribute("width", String(stageRect.width));
    overlay.setAttribute("height", String(stageRect.height));

    const signalLabels = new Map();
    const labelEntriesByRef = new Map();
    const boardProfile = activeGpioBoardProfile();
    const boardLabelDefaults = editableBoardLabels(boardProfile);
    const resolvedBoardLabels = resolvePeripheralDiagramNodeLabels(
      state,
      peripheralDiagramBoardNodeId(boardProfile),
      boardLabelDefaults,
    );
    const boardDefaultsById = new Map(boardLabelDefaults.map((entry) => [entry.id, entry]));
    const dedupedResolvedBoardLabels = [];
    const railLabelIndexByKey = new Map();

    resolvedBoardLabels.forEach((entry) => {
      const railLabel = String(entry.label || "").trim().toUpperCase();
      const isRailLabel = railLabel && (isPositivePowerSignal(railLabel) || isGroundSignal(railLabel));
      if (!isRailLabel) {
        dedupedResolvedBoardLabels.push(entry);
        return;
      }

      const existingIndex = railLabelIndexByKey.get(railLabel);
      if (existingIndex === undefined) {
        railLabelIndexByKey.set(railLabel, dedupedResolvedBoardLabels.length);
        dedupedResolvedBoardLabels.push(entry);
        return;
      }

      if (entry.isCustom) {
        dedupedResolvedBoardLabels[existingIndex] = entry;
      }
    });
    const resolvedBoardLabelsByTarget = new Map();

    dedupedResolvedBoardLabels.forEach((entry) => {
      const fallback = boardDefaultsById.get(entry.id) || null;
      const railLabel = String(entry.label || fallback?.label || "").trim().toUpperCase();
      const isRailLabel = railLabel && (isPositivePowerSignal(railLabel) || isGroundSignal(railLabel));
      const resolvedEntry = {
        labelKey: entry.id,
        nodeId: peripheralDiagramBoardNodeId(boardProfile),
        label: entry.label,
        pin: Number.isFinite(Number(fallback?.pin)) ? Number(fallback.pin) : null,
        palette: peripheralDiagramLabelPalette(entry.label),
        nodeRect: boardRect,
        layout: {
          ...(fallback || { xFactor: 0, yFactor: 0, rotation: 0 }),
          xFactor: entry.xFactor,
          yFactor: entry.yFactor,
          rotation: entry.rotation,
        },
      };
      signalLabels.set(`board:${entry.id}`, resolvedEntry);
      labelEntriesByRef.set(labelReferenceKey(resolvedEntry.nodeId, resolvedEntry.labelKey), resolvedEntry);

      if (Number.isFinite(Number(fallback?.pin))) {
        resolvedBoardLabelsByTarget.set(`gpio:${Number(fallback.pin)}`, resolvedEntry);
      }
      if (railLabel && (isRailLabel || !Number.isFinite(Number(fallback?.pin)))) {
        resolvedBoardLabelsByTarget.set(`rail:${railLabel}`, resolvedEntry);
      }
    });

    const groupedConnections = new Map();
    for (const connection of collectConnections(nodes)) {
      if (isConnectionHidden(state, connectionStorageKey(connection))) {
        continue;
      }
      if (!groupedConnections.has(connection.nodeId)) {
        groupedConnections.set(connection.nodeId, []);
      }
      groupedConnections.get(connection.nodeId).push(connection);
    }

    const resolvedLabelsByNode = new Map();

    for (const node of nodes) {
      const nodeRect = nodeRects.get(node.id);
      const visualRect = visualRects.get(node.id) || nodeRect;
      const connections = groupedConnections.get(node.id) || [];
      if (!nodeRect) {
        continue;
      }

      const nodeSignalLabels = new Map();

      if (isLimitSwitchNode(node)) {
        const defaultPins = Array.isArray(node.pins)
          ? node.pins.filter((pinLabel) => ["COM", "NC", "NO"].includes(signalKey(pinLabel)))
          : [];
        defaultPins.forEach((pinLabel, index) => {
          const label = normalizeSignalLabel(pinLabel);
          const labelId = peripheralDiagramLabelId(label);
          if (nodeSignalLabels.has(labelId)) {
            return;
          }
          const defaultAnchor = nodeAnchorForConnection(nodeRect, boardRect, index, Math.max(defaultPins.length, 1));
          nodeSignalLabels.set(labelId, {
            id: labelId,
            label,
            palette: peripheralDiagramLabelPalette(label),
            defaultLayout: signalLabelDefaultLayout(visualRect, defaultAnchor, label),
          });
        });
      }

      connections.forEach((connection, index) => {
        const defaultAnchor = nodeAnchorForConnection(nodeRect, boardRect, index, connections.length);
        const labelId = peripheralDiagramLabelId(connection.signalLabel);
        if (!nodeSignalLabels.has(labelId)) {
          nodeSignalLabels.set(labelId, {
            id: labelId,
            label: normalizeSignalLabel(connection.signalLabel),
            dividerMaximum:node.groupKey==='sensor'&&String(node.profileValue||'').includes('battery-voltage-divider')&&signalKey(connection.signalLabel)==='SIGNAL'?voltageDividerMaximum(state.settings?.battery):null,
            palette: classifyWireColor(connection),
            defaultLayout: signalLabelDefaultLayout(visualRect, defaultAnchor, connection.signalLabel),
          });
        }
      });

      const resolvedLabels = resolvePeripheralDiagramNodeLabels(state, node.id, [...nodeSignalLabels.values()].map((entry, index) => ({
        id: entry.id,
        label: entry.label,
        xFactor: entry.defaultLayout.xFactor,
        yFactor: entry.defaultLayout.yFactor,
        rotation: entry.defaultLayout.rotation,
        order: index,
      })));

      const resolvedLabelsById = new Map();
      resolvedLabels.forEach((entry, index) => {
        const source = nodeSignalLabels.get(entry.id);
        const baseLayout = {
          ...(source?.defaultLayout || floatingLabelDefaultLayout(nodeRect, index, resolvedLabels.length)),
          xFactor: entry.xFactor,
          yFactor: entry.yFactor,
          rotation: entry.rotation,
        };
        const layout = entry.coordinateSpace === "visual"
          ? baseLayout
          : convertLayoutToVisualSpace(baseLayout, nodeRect, visualRect);
        const resolvedEntry = {
          labelKey: entry.id,
          nodeId: node.id,
          label: entry.label,
          palette: peripheralDiagramLabelPalette(entry.label, source?.palette),
          dividerMaximum:source?.dividerMaximum,
          nodeRect: visualRect,
          layout,
        };
        resolvedLabelsById.set(entry.id, resolvedEntry);
        signalLabels.set(`${node.id}:${entry.id}`, resolvedEntry);
        labelEntriesByRef.set(labelReferenceKey(resolvedEntry.nodeId, resolvedEntry.labelKey), resolvedEntry);
      });

      resolvedLabelsByNode.set(node.id, resolvedLabelsById);
    }

    const customLabelConnections = readCustomLabelConnections(state).filter((connection) => (
      labelEntriesByRef.has(labelReferenceKey(connection.fromNodeId, connection.fromLabelKey))
      && labelEntriesByRef.has(labelReferenceKey(connection.toNodeId, connection.toLabelKey))
    ));

    for (const [nodeId, connections] of groupedConnections.entries()) {
      connections.forEach((connection) => {
        const palette = classifyWireColor(connection);
        const sourceRef = labelReferenceKey(nodeId, peripheralDiagramLabelId(connection.signalLabel));
        const sourceEntry = labelEntriesByRef.get(sourceRef);
        if (sourceEntry) {
          sourceEntry.palette = palette;
        }
        const boardEntry = resolvedBoardLabelsByTarget.get(boardTargetKey(connection));
        if (boardEntry) {
          boardEntry.palette = palette;
        }
      });
    }

    customLabelConnections.forEach((connection) => {
      const sourceEntry = labelEntriesByRef.get(labelReferenceKey(connection.fromNodeId, connection.fromLabelKey));
      const targetEntry = labelEntriesByRef.get(labelReferenceKey(connection.toNodeId, connection.toLabelKey));
      if (sourceEntry && targetEntry) {
        targetEntry.palette = normalizeWirePalette(sourceEntry.palette);
      }
    });

    renderSignalLabels(labelLayer, [...signalLabels.values()]);
    const actualLabelRects = renderedLabelRects(labelLayer, stageRect);
    lastRenderedLabelEntries = new Map(labelEntriesByRef);
    lastRenderedLabelRects = new Map(actualLabelRects);
    bindLabelConnectionInteractions(labelLayer, overlay);

    for (const node of nodes) {
      const nodeRect = nodeRects.get(node.id);
      const visualRect = visualRects.get(node.id) || nodeRect;
      const connections = groupedConnections.get(node.id) || [];
      if (!nodeRect) {
        continue;
      }

      const resolvedLabelsById = resolvedLabelsByNode.get(node.id) || new Map();

      connections.forEach((connection, index) => {
        const defaultAnchor = nodeAnchorForConnection(nodeRect, boardRect, index, connections.length);
        const boardLabelEntry = resolvedBoardLabelsByTarget.get(boardTargetKey(connection)) || null;
        const boardAnchor = boardLabelEntry
          ? {
            ...boardLabelAnchor(
              boardLabelEntry,
              boardRect,
              stageRect.width,
              stageRect.height,
              actualLabelRects.get(`${boardLabelEntry.nodeId}:${boardLabelEntry.labelKey}`) || null,
            ),
            boardLabel: boardLabelEntry.label,
          }
          : chooseBoardAnchor(anchorCandidates, connection, defaultAnchor);
        if (!boardAnchor) {
          return;
        }
        const resolvedLabel = resolvedLabelsById.get(peripheralDiagramLabelId(connection.signalLabel));
        const nodeAnchor = resolvedLabel
          ? labelAnchorAwayFromOwner(
            resolvedLabel,
            visualRect,
            stageRect.width,
            stageRect.height,
            actualLabelRects.get(`${resolvedLabel.nodeId}:${resolvedLabel.labelKey}`) || null,
          )
          : defaultAnchor;
        if (!hasFiniteAnchorPoint(nodeAnchor) || !hasFiniteAnchorPoint(boardAnchor)) {
          return;
        }
        const palette = classifyWireColor(connection);
        const connectionKey = connectionStorageKey(connection);
        const savedCurvePoints = readStoredWireCurve(state, connectionKey, stageRect);
        const geometry = connectionGeometry(nodeAnchor, boardAnchor, visualRect, boardRect, savedCurvePoints);
        if (!hasFiniteAnchorPoint(geometry?.controlPoint) || !hasFiniteAnchorPoint(geometry?.handlePoint)) {
          return;
        }

        const glow = createSvgElement("path");
        glow.setAttribute("class", "peripheral-diagram-wire peripheral-diagram-wire-glow");
        glow.setAttribute("d", geometry.path);
        glow.setAttribute("stroke", palette.glow);

        const path = createSvgElement("path");
        path.setAttribute("class", "peripheral-diagram-wire");
        path.setAttribute("d", geometry.path);
        path.setAttribute("stroke", palette.stroke);

        const connectionGroup = createSvgElement("g");
        connectionGroup.setAttribute("class", "peripheral-diagram-wire-connection");
        connectionGroup.dataset.connectionKind = "auto";
        connectionGroup.dataset.nodeId = String(node.id);
        connectionGroup.dataset.signal = signalKey(connection.signalLabel);
        connectionGroup.dataset.connectionType = String(connection.type || "gpio");
        connectionGroup.dataset.board = String(boardAnchor.boardLabel || "");
        connectionGroup.dataset.connectionKey = connectionKey;
        connectionGroup.dataset.nodeAnchorX = String(nodeAnchor.x);
        connectionGroup.dataset.nodeAnchorY = String(nodeAnchor.y);
        connectionGroup.dataset.nodeAnchorSide = String(nodeAnchor.side || "right");
        connectionGroup.dataset.boardAnchorX = String(boardAnchor.x);
        connectionGroup.dataset.boardAnchorY = String(boardAnchor.y);
        connectionGroup.dataset.boardAnchorSide = String(boardAnchor.side || "left");
        connectionGroup.dataset.nodeRectLeft = String(visualRect.left);
        connectionGroup.dataset.nodeRectTop = String(visualRect.top);
        connectionGroup.dataset.nodeRectWidth = String(visualRect.width);
        connectionGroup.dataset.nodeRectHeight = String(visualRect.height);
        connectionGroup.dataset.boardRectLeft = String(boardRect.left);
        connectionGroup.dataset.boardRectTop = String(boardRect.top);
        connectionGroup.dataset.boardRectWidth = String(boardRect.width);
        connectionGroup.dataset.boardRectHeight = String(boardRect.height);
        connectionGroup.dataset.routePoints = JSON.stringify(geometry.manualCurvePoints);
        connectionGroup.dataset.controlX = String(geometry.controlPoint.x);
        connectionGroup.dataset.controlY = String(geometry.controlPoint.y);

        const hit = createSvgElement("path");
        hit.setAttribute("class", "peripheral-diagram-wire-hit");
        hit.setAttribute("d", geometry.path);
        hit.addEventListener("pointerdown", (event) => beginCurvePointerDrag(event, overlay, connectionGroup, connectionKey, -1, true));
        hit.addEventListener("contextmenu", (event) => {
          event.preventDefault();
          event.stopPropagation();
          deleteWireConnection(connectionGroup);
        });

        const title = createSvgElement("title");
        title.textContent = `${normalizeSignalLabel(connection.signalLabel)} -> ${boardAnchor.boardLabel}`;

        connectionGroup.addEventListener("pointerenter", () => setActiveConnection(overlay, connectionGroup));
        connectionGroup.addEventListener("pointerleave", () => {
          if (curveDragState.key !== connectionKey) {
            setActiveConnection(overlay, null);
          }
        });

        connectionGroup.appendChild(title);
        connectionGroup.appendChild(glow);
        connectionGroup.appendChild(path);
        connectionGroup.appendChild(hit);
        renderCurveHandles(connectionGroup, overlay, connectionKey, geometry);
        let boardEndpointMarker = null;
        if (!boardLabelEntry) {
          boardEndpointMarker = drawBoardMarker(connectionGroup, boardAnchor, palette);
          drawBoardBadge(connectionGroup, boardAnchor, boardAnchor.boardLabel, palette);
        }
        drawNodeMarker(connectionGroup, nodeAnchor, palette);
        if (boardLabelEntry) {
          boardEndpointMarker = drawNodeMarker(connectionGroup, boardAnchor, palette);
        }
        if (connection.type === "gpio" && boardEndpointMarker) {
          boardEndpointMarker.classList.add("peripheral-diagram-wire-endpoint-handle");
          boardEndpointMarker.addEventListener("pointerdown", (event) => beginBoardEndpointDrag(event, overlay, connectionGroup));
        }
        overlay.appendChild(connectionGroup);
      });
    }

    customLabelConnections.forEach((connection) => {
      const sourceEntry = labelEntriesByRef.get(labelReferenceKey(connection.fromNodeId, connection.fromLabelKey));
      const targetEntry = labelEntriesByRef.get(labelReferenceKey(connection.toNodeId, connection.toLabelKey));
      if (!sourceEntry || !targetEntry) {
        return;
      }
      const sourceAnchor = labelAnchorAwayFromOwner(
        sourceEntry,
        sourceEntry.nodeRect,
        stageRect.width,
        stageRect.height,
        actualLabelRects.get(`${sourceEntry.nodeId}:${sourceEntry.labelKey}`) || null,
      );
      const targetAnchor = labelAnchorAwayFromOwner(
        targetEntry,
        targetEntry.nodeRect,
        stageRect.width,
        stageRect.height,
        actualLabelRects.get(`${targetEntry.nodeId}:${targetEntry.labelKey}`) || null,
      );
      if (!hasFiniteAnchorPoint(sourceAnchor) || !hasFiniteAnchorPoint(targetAnchor)) {
        return;
      }

      const palette = normalizeWirePalette(sourceEntry.palette);
      const connectionKey = `custom:${customLabelConnectionKey(connection)}`;
      const savedCurvePoints = readStoredWireCurve(state, connectionKey, stageRect);
      const geometry = connectionGeometry(sourceAnchor, targetAnchor, sourceEntry.nodeRect, targetEntry.nodeRect, savedCurvePoints);
      if (!hasFiniteAnchorPoint(geometry?.controlPoint) || !hasFiniteAnchorPoint(geometry?.handlePoint)) {
        return;
      }

      const glow = createSvgElement("path");
      glow.setAttribute("class", "peripheral-diagram-wire peripheral-diagram-wire-glow");
      glow.setAttribute("d", geometry.path);
      glow.setAttribute("stroke", palette.glow);

      const path = createSvgElement("path");
      path.setAttribute("class", "peripheral-diagram-wire");
      path.setAttribute("d", geometry.path);
      path.setAttribute("stroke", palette.stroke);

      const connectionGroup = createSvgElement("g");
      connectionGroup.setAttribute("class", "peripheral-diagram-wire-connection");
      connectionGroup.dataset.connectionKind = "custom";
      connectionGroup.dataset.nodeId = String(sourceEntry.nodeId);
      connectionGroup.dataset.connectionType = "custom";
      connectionGroup.dataset.signal = signalKey(sourceEntry.label);
      connectionGroup.dataset.board = String(targetEntry.label || "");
      connectionGroup.dataset.connectionKey = connectionKey;
      connectionGroup.dataset.nodeAnchorX = String(sourceAnchor.x);
      connectionGroup.dataset.nodeAnchorY = String(sourceAnchor.y);
      connectionGroup.dataset.nodeAnchorSide = String(sourceAnchor.side || "right");
      connectionGroup.dataset.boardAnchorX = String(targetAnchor.x);
      connectionGroup.dataset.boardAnchorY = String(targetAnchor.y);
      connectionGroup.dataset.boardAnchorSide = String(targetAnchor.side || "left");
      connectionGroup.dataset.nodeRectLeft = String(sourceEntry.nodeRect.left);
      connectionGroup.dataset.nodeRectTop = String(sourceEntry.nodeRect.top);
      connectionGroup.dataset.nodeRectWidth = String(sourceEntry.nodeRect.width);
      connectionGroup.dataset.nodeRectHeight = String(sourceEntry.nodeRect.height);
      connectionGroup.dataset.boardRectLeft = String(targetEntry.nodeRect.left);
      connectionGroup.dataset.boardRectTop = String(targetEntry.nodeRect.top);
      connectionGroup.dataset.boardRectWidth = String(targetEntry.nodeRect.width);
      connectionGroup.dataset.boardRectHeight = String(targetEntry.nodeRect.height);
      connectionGroup.dataset.routePoints = JSON.stringify(geometry.manualCurvePoints);
      connectionGroup.dataset.controlX = String(geometry.controlPoint.x);
      connectionGroup.dataset.controlY = String(geometry.controlPoint.y);

      const hit = createSvgElement("path");
      hit.setAttribute("class", "peripheral-diagram-wire-hit");
      hit.setAttribute("d", geometry.path);
      hit.addEventListener("pointerdown", (event) => beginCurvePointerDrag(event, overlay, connectionGroup, connectionKey, -1, true));
      hit.addEventListener("contextmenu", (event) => {
        event.preventDefault();
        event.stopPropagation();
        deleteWireConnection(connectionGroup);
      });

      const title = createSvgElement("title");
      title.textContent = `${normalizeSignalLabel(sourceEntry.label)} -> ${normalizeSignalLabel(targetEntry.label)}`;

      connectionGroup.addEventListener("pointerenter", () => setActiveConnection(overlay, connectionGroup));
      connectionGroup.addEventListener("pointerleave", () => {
        if (curveDragState.key !== connectionKey) {
          setActiveConnection(overlay, null);
        }
      });

      connectionGroup.appendChild(title);
      connectionGroup.appendChild(glow);
      connectionGroup.appendChild(path);
      connectionGroup.appendChild(hit);
      renderCurveHandles(connectionGroup, overlay, connectionKey, geometry);
      drawNodeMarker(connectionGroup, sourceAnchor, palette);
      drawNodeMarker(connectionGroup, targetAnchor, palette);
      overlay.appendChild(connectionGroup);
    });
  }

  return {
    render,
    rewireFromLabels,
    resetManualWireCurves,
  };
}
