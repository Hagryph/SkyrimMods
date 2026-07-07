const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const outPath = path.join(root, 'assets', 'HagVampire.esp');

const MASTER_COUNT = 3;
const FRESH_BLOOD_POTION_BASE = (MASTER_COUNT << 24) | 0x000800;
const STALE_BLOOD_POTION_BASE = (MASTER_COUNT << 24) | 0x000840;
const BLOOD_POTION_EXTRACT_MIN = 1;
const BLOOD_POTION_EXTRACT_MAX = 34;
const BLOOD_POTION_EXTRACT_HEALTH_SCALE = 1000;

const FORM_ITM_POTION_USE_SOUND = 0x00106614;
const FORM_ITM_POTION_PICKUP_SOUND = 0x0003EDBD;
const FORM_ITM_POTION_DROP_SOUND = 0x0003EDC0;
const FORM_DLC1_BLOOD_POTION_EFFECT = 0x02018EF4;
const FORM_DLC1_RESTORE_HEALTH_BLOOD_EFFECT = 0x02013812;

function ascii(text) {
  return Buffer.from(text, 'ascii');
}

function zstr(text) {
  return Buffer.from(`${text}\0`, 'utf8');
}

function u16(value) {
  const b = Buffer.alloc(2);
  b.writeUInt16LE(value >>> 0);
  return b;
}

function u32(value) {
  const b = Buffer.alloc(4);
  b.writeUInt32LE(value >>> 0);
  return b;
}

function f32(value) {
  const b = Buffer.alloc(4);
  b.writeFloatLE(value);
  return b;
}

function concat(parts) {
  return Buffer.concat(parts.filter(Boolean));
}

function subrecord(type, data) {
  const body = Buffer.isBuffer(data) ? data : zstr(data);
  if (body.length > 0xffff) {
    throw new Error(`subrecord ${type} too large: ${body.length}`);
  }
  return concat([ascii(type), u16(body.length), body]);
}

function record(type, formID, fields, flags = 0) {
  const body = concat(fields);
  return concat([
    ascii(type),
    u32(body.length),
    u32(flags),
    u32(formID),
    u32(0),
    u16(44),
    u16(0),
    body,
  ]);
}

function topGroup(type, records) {
  const body = concat(records);
  return concat([
    ascii('GRUP'),
    u32(24 + body.length),
    ascii(type),
    u32(0),
    u16(0),
    u16(44),
    u32(0),
    body,
  ]);
}

function master(name) {
  return concat([
    subrecord('MAST', name),
    subrecord('DATA', Buffer.alloc(8)),
  ]);
}

function enit(value) {
  return concat([
    u32(value),
    u32(1),
    u32(0),
    f32(0),
    u32(FORM_ITM_POTION_USE_SOUND),
  ]);
}

function efit(magnitude, area = 0, duration = 0) {
  return concat([
    f32(magnitude),
    u32(area),
    u32(duration),
  ]);
}

function potionRecord({ formID, edid, name, value, healthMagnitude, extractReward }) {
  return record('ALCH', formID, [
    subrecord('EDID', edid),
    subrecord('OBND', Buffer.from('f3fff3ff00000d000d001400', 'hex')),
    subrecord('FULL', name),
    subrecord('MODL', 'DLC01\\Clutter\\DLC01BloodPotion.nif'),
    subrecord('YNAM', u32(FORM_ITM_POTION_PICKUP_SOUND)),
    subrecord('ZNAM', u32(FORM_ITM_POTION_DROP_SOUND)),
    subrecord('DATA', f32(0.5)),
    subrecord('ENIT', enit(value)),
    subrecord('EFID', u32(FORM_DLC1_BLOOD_POTION_EFFECT)),
    subrecord('EFIT', efit(extractReward)),
    subrecord('EFID', u32(FORM_DLC1_RESTORE_HEALTH_BLOOD_EFFECT)),
    subrecord('EFIT', efit(healthMagnitude)),
  ]);
}

const tes4 = record('TES4', 0, [
  subrecord('HEDR', concat([f32(1.7), u32(2), u32(0x000862)])),
  subrecord('CNAM', 'Hagryph'),
  subrecord('SNAM', 'HagVampire owned blood potion records.'),
  master('Skyrim.esm'),
  master('Update.esm'),
  master('Dawnguard.esm'),
]);

function encodedHealth(baseHealth, extractReward) {
  return baseHealth + (extractReward / BLOOD_POTION_EXTRACT_HEALTH_SCALE);
}

const potions = [];
for (let extractReward = BLOOD_POTION_EXTRACT_MIN; extractReward <= BLOOD_POTION_EXTRACT_MAX; extractReward += 1) {
  potions.push(potionRecord({
    formID: FRESH_BLOOD_POTION_BASE + (extractReward - BLOOD_POTION_EXTRACT_MIN),
    edid: `HagVampireBloodPotionExtract${extractReward}`,
    name: 'Extracted Blood Potion',
    value: 75,
    healthMagnitude: encodedHealth(100, extractReward),
    extractReward,
  }));
  potions.push(potionRecord({
    formID: STALE_BLOOD_POTION_BASE + (extractReward - BLOOD_POTION_EXTRACT_MIN),
    edid: `HagVampireStaleBloodPotionExtract${extractReward}`,
    name: 'Stale Blood Potion',
    value: 25,
    healthMagnitude: encodedHealth(50, extractReward),
    extractReward,
  }));
}

fs.mkdirSync(path.dirname(outPath), { recursive: true });
fs.writeFileSync(outPath, concat([tes4, topGroup('ALCH', potions)]));
console.log(`wrote ${outPath}`);
