import { UeMcpTask } from 'ue-mcp/task';

export default class FabEditorTask extends UeMcpTask {
  get taskName() { return 'fab_editor.run'; }
  async execute() {
    const keys = ['operation', 'operationId', 'assetId', 'query', 'batchSize', 'limit', 'offset', 'browserId', 'snapshotId', 'elementId', 'value', 'confirmDownload', 'deliveryMode'];
    const params = Object.fromEntries(keys.filter(key => this.options[key] !== undefined).map(key => [key, this.options[key]]));
    const result = await this.bridge.call('fab_editor', params, 30000);
    if (!result || typeof result !== 'object') return { success: false, error: 'Invalid native Fab response.' };
    return { success: result.success !== false, data: result, ...(result.success === false ? { error: result.error || 'Fab operation failed.' } : {}) };
  }
}
