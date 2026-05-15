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

  startListening() {
    this.stream.on("data", (response: IStreamingRecognizeResponse) => {
      console.log(response.results?.[0].alternatives?.[0].transcript);
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
