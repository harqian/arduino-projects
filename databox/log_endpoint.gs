const LOG_SHEET_ID = '1Z7KiKy2qKI4xOwkHMtO-VUCgjTkaaF9fFL4BU8V-S8Q';
const DATA_SHEET_NAME = 'Sheet1';
const DEBUG_SHEET_NAME = 'Logs';
const TRANSCRIPTION_THRESHOLD_MS = 150;

function logToSheet(message) {
  SpreadsheetApp.openById(LOG_SHEET_ID)
    .getSheetByName(DEBUG_SHEET_NAME)
    .appendRow([new Date(), message]);
}

function appendDataRow(columnKey, value) {
  SpreadsheetApp.openById(LOG_SHEET_ID)
    .getSheetByName(DATA_SHEET_NAME)
    .appendRow([new Date(), columnKey, value]);
}

function parsePayload_(e) {
  if (!e || !e.postData || !e.postData.contents) {
    throw new Error('Missing POST body');
  }

  return JSON.parse(e.postData.contents);
}

function validatePayload_(payload) {
  if (payload.score_1 === undefined || payload.score_2 === undefined || payload.hold_duration_ms === undefined) {
    throw new Error('Payload must include score_1, score_2, and hold_duration_ms');
  }
}

function doPost(e) {
  try {
    const payload = parsePayload_(e);
    validatePayload_(payload);

    const columnKey = payload.score_1;
    const numericValue = payload.score_2;
    const holdDurationMs = Number(payload.hold_duration_ms);

    let storedValue = numericValue;

    if (holdDurationMs > TRANSCRIPTION_THRESHOLD_MS) {
      if (!payload.audio_base64) {
        throw new Error('Missing audio_base64 for long-hold transcription');
      }

      storedValue = transcribeWithGemini(payload.audio_base64);
      logToSheet('Stored transcription for column ' + columnKey);
    } else {
      logToSheet('Stored numeric value for column ' + columnKey);
    }

    appendDataRow(columnKey, storedValue);

    return ContentService.createTextOutput(JSON.stringify({
      success: true,
      column_key: columnKey,
      stored_value: storedValue,
      hold_duration_ms: holdDurationMs,
      transcribed: holdDurationMs > TRANSCRIPTION_THRESHOLD_MS
    })).setMimeType(ContentService.MimeType.JSON);
  } catch (err) {
    logToSheet('doPost error: ' + (err.stack || err.message));
    return ContentService.createTextOutput(JSON.stringify({
      success: false,
      error: err.message
    })).setMimeType(ContentService.MimeType.JSON);
  }
}

function transcribeWithGemini(base64Audio) {
  const apiKey = PropertiesService.getScriptProperties().getProperty('GEMINI_API_KEY');
  const url = 'https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=' + apiKey;

  const payload = {
    contents: [{
      parts: [
        { text: 'Transcribe this audio exactly. Return only the transcription, no commentary.' },
        {
          inlineData: {
            mimeType: 'audio/wav',
            data: base64Audio
          }
        }
      ]
    }]
  };

  const response = UrlFetchApp.fetch(url, {
    method: 'POST',
    contentType: 'application/json',
    payload: JSON.stringify(payload),
    muteHttpExceptions: true
  });

  const result = JSON.parse(response.getContentText());

  if (result.error) {
    throw new Error(result.error.message);
  }

  return result.candidates?.[0]?.content?.parts?.[0]?.text || 'No transcription returned';
}
