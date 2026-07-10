import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import '../index.css';
import './drum.css';
import { DrumApp } from './DrumApp';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <DrumApp />
  </StrictMode>,
);
