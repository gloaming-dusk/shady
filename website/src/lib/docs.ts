export const docOrder = [
  'API',
  'LUA_API',
  'HEADLESS_API',
  'IPC_API',
  'C_PLUGIN_API',
  'SHADER_API',
  'ARCHITECTURE'
];

export const docLabels: Record<string, string> = {
  API: 'API overview',
  LUA_API: 'Lua API',
  HEADLESS_API: 'Headless API',
  IPC_API: 'IPC API',
  SHELL_DESIGN: 'Shell design',
  C_PLUGIN_API: 'C plugin API',
  SHADER_API: 'Shader API',
  ARCHITECTURE: 'Architecture'
};

export function docStem(id: string) {
  return id.replace(/\.md$/i, '').split('/').pop() ?? id;
}

/* Content ids arrive lowercased (c_plugin_api / c-plugin-api); the label and
 * order tables use the repository file stems (C_PLUGIN_API). */
function docKey(id: string) {
  return docStem(id).replace(/-/g, '_').toUpperCase();
}

export function docSlug(id: string) {
  return docStem(id).replace(/_/g, '-').toLowerCase();
}

export function docLabel(id: string) {
  const stem = docStem(id);
  return docLabels[docKey(id)] ?? stem.replace(/[_-]/g, ' ');
}

export function docSort(a: { id: string }, b: { id: string }) {
  const ai = docOrder.indexOf(docKey(a.id));
  const bi = docOrder.indexOf(docKey(b.id));
  const aRank = ai === -1 ? Number.MAX_SAFE_INTEGER : ai;
  const bRank = bi === -1 ? Number.MAX_SAFE_INTEGER : bi;
  return aRank - bRank || docLabel(a.id).localeCompare(docLabel(b.id));
}
