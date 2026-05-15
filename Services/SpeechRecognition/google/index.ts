import { SpeechClient } from "@google-cloud/speech";
import type {
  StreamingRecognizeConfig,
  IStreamingRecognizeResponse,
  IStreamingRecognitionConfig,
} from "./types.ts";

const config: StreamingRecognizeConfig = {
  encoding: "LINEAR16",
  sampleRateHertz: 16000,
  languageCode: "pt-BR",
};

const streamingRecognizeOptions: IStreamingRecognitionConfig = {
  config,
  interimResults: true,
};

class GoogleSpeechToText {
  private speechClient: SpeechClient;
  public stream: any;
  constructor() {
    this.speechClient = new SpeechClient();
  }

  createNewStream() {
    const stream = this.speechClient.streamingRecognize(
      streamingRecognizeOptions,
    );
    this.stream = stream;
  }

  startListening(onComplete: (text: string) => void, debounceMs = 500) {
    let buffer = "";
    let timer: NodeJS.Timeout | null = null;

    this.stream.on("data", (response: IStreamingRecognizeResponse) => {
      const result = response.results?.[0];
      if (!result?.isFinal) return;

      const transcript = result.alternatives?.[0]?.transcript ?? "";
      buffer += (buffer ? " " : "") + transcript;

      if (timer) clearTimeout(timer);
      timer = setTimeout(() => {
        onComplete(buffer.trim());
        buffer = "";
        timer = null;
      }, debounceMs);
    });
    this.stream.on("error", (err: Error) => {
      console.log(err);
      throw err;
    });
    this.stream.on("end", () => {
      console.log("Transcricao concluida.");
    });
  }
}

export default GoogleSpeechToText;
