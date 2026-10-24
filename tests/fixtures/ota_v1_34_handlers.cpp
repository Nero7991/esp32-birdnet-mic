// Frozen v1.34 handlers: the initial upgrade must work before Prepare exists.
static void legacyhttpOtaInstall() {
    if (!requireMutationAuth()) return;

    String latestVersion;
    String versionError;
    if (!fetchLatestOtaVersion(true, latestVersion, versionError)) {
        webui_pushLog(String("OTA install blocked: version check failed: ") + versionError);
        sendOtaPage(String("Update blocked: ") + versionError, false);
        return;
    }
    if (!isNewerFirmwareAvailable(latestVersion)) {
        String reason = String("No newer firmware is available. Installed v") + FW_VERSION_STR +
                        ", latest v" + latestVersion + ".";
        webui_pushLog(String("OTA install blocked: ") + reason);
        sendOtaPage(reason, false);
        return;
    }

    String url = getDefaultOtaUrl();
    String error;
    bool ok = installOtaFromUrl(url, error);
    if (ok) {
        web.sendHeader("X-ESP32MIC-OTA-Reboot", "1");
        sendOtaPage(F("Firmware installed. Device will reboot now."), true);
        scheduleReboot(false, 700);
    } else {
        webui_pushLog(String("OTA pull failed: ") + error);
        sendOtaPage(String("Update failed: ") + error, false);
    }
}

static void legacyhttpOtaUploadDone() {
    if (!requireMutationAuth()) return;

    if (otaUploadOk) {
        web.sendHeader("X-ESP32MIC-OTA-Reboot", "1");
        sendOtaPage(F("Firmware uploaded and installed. Device will reboot now."), true);
        scheduleReboot(false, 700);
    } else {
        endOtaUpdate(false);
        sendOtaPage(String("Upload failed: ") + otaUploadError, false);
    }
}

static void legacyhttpOtaUploadChunk() {
    HTTPUpload &upload = web.upload();
    if (upload.status == UPLOAD_FILE_START) {
        otaUploadOk = false;
        otaUploadError = "";
        if (!hasMutationAuth()) {
            otaUploadError = F("forbidden");
            Update.abort();
            return;
        }
        if (isLikelyMergedFirmwareName(upload.filename)) {
            otaUploadError = F("This looks like a merged USB firmware image. OTA needs the matching app-only firmware-app-*.bin file.");
            Update.abort();
            return;
        }
        beginOtaUpdate();
        webui_pushLog(String("OTA upload start: ") + upload.filename);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            otaUploadError = String("Update begin failed: ") + Update.errorString();
            endOtaUpdate(false);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (otaUploadError.length() == 0) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                otaUploadError = String("Flash write failed: ") + Update.errorString();
                Update.abort();
                endOtaUpdate(false);
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (otaUploadError.length() == 0) {
            if (!validateOtaImageSize(upload.totalSize, otaUploadError)) {
                Update.abort();
            } else if (!Update.end(true)) {
                otaUploadError = String("Update end failed: ") + Update.errorString();
            } else if (!Update.isFinished()) {
                otaUploadError = F("Update was not fully written.");
            } else {
                otaUploadOk = true;
                webui_pushLog(String("OTA upload installed, bytes=") + String(upload.totalSize));
            }
        }
        endOtaUpdate(otaUploadOk);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        otaUploadError = F("Upload aborted.");
        Update.abort();
        endOtaUpdate(false);
    }
}
