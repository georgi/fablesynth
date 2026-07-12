import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import '../index.css';
import '../drum/drum.css';
import '../bass/bass.css';
import './seq.css';
import { SeqApp } from './SeqApp';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <SeqApp />
  </StrictMode>,
);
