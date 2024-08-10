#!/usr/bin/env node
import * as cdk from "aws-cdk-lib";
import { SelfUpdateTopStack } from "../lib/SelfUpdateTopStack";

const app = new cdk.App();

new SelfUpdateTopStack(app, "garage-door-pipeline", {
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: process.env.CDK_DEFAULT_REGION,
  },
});
