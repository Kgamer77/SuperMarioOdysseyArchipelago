/// <reference types="vite/client" />

declare module '*.csv?raw' {
  const value: string;
  export default value;
}
