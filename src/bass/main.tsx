import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import '../index.css';
import './bass.css';
import { BassApp } from './BassApp';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <BassApp />
  </StrictMode>,
);
