import { Storage } from "@google-cloud/storage";

async function authenticateImplicitWithAdc() {
  // This snippet demonstrates how to list buckets.
  // NOTE: Replace the client created below with the client required for your application.
  // Note that the credentials are not specified when constructing the client.
  // The client library finds your credentials using ADC.
  const storage = new Storage({
    projectId,
  });
  const [buckets] = await storage.getBuckets();
  console.log("Buckets:");

  for (const bucket of buckets) {
    console.log(`- ${bucket.name}`);
  }

  console.log("Listed all storage buckets.");
}
