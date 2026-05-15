import { protos } from "@google-cloud/speech";

type StreamingRecognizeConfig =
  protos.google.cloud.speech.v1.IRecognitionConfig;
type IStreamingRecognizeResponse =
  protos.google.cloud.speech.v1.IStreamingRecognizeResponse;
type IStreamingRecognitionConfig =
  protos.google.cloud.speech.v1.IStreamingRecognitionConfig;

export type {
  StreamingRecognizeConfig,
  IStreamingRecognizeResponse,
  IStreamingRecognitionConfig,
};
