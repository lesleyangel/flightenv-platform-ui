import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  define: {
    "process.env.NODE_ENV": JSON.stringify("production"),
    "process.env": "{}",
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    lib: {
      entry: "src/reactflow-workflow-editor.jsx",
      name: "FlightEnvReactFlowEditorBundle",
      formats: ["iife"],
      fileName: () => "flightenv-reactflow-editor.js",
      cssFileName: "flightenv-reactflow-editor",
    },
  },
});
