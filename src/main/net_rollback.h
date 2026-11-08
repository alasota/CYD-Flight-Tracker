// net_rollback — marks a freshly installed image valid once it has proven
// itself, or restarts it to roll back when the display loop has stopped
// (ota_update's shouldMarkAppValid() / shouldRestartForRollback()).
//
// Device-only (net_task, esp_ota_ops).
#pragma once

// `pending`: the running image still has to be marked valid.
void netRollbackBegin(bool pending);
// Every pass of the network task.
void markAppValidWhenStable();
